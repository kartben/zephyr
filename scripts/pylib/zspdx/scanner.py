# Copyright (c) 2020, 2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import logging
import os
import re
import threading
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass

import tqdm
from reuse.project import Project

from .licenses import LICENSES
from .util import get_hashes

_logger = logging.getLogger(__name__)

# Cache of reuse Project instances keyed by directory. Building a Project
# scans the directory for REUSE licensing config (.reuse/dep5, REUSE.toml,
# LICENSES/, ...), so reusing one per directory instead of rebuilding it for
# every single file is a large speedup. Guarded by a lock because scanning
# runs across a thread pool.
_reuse_projects = {}
_reuse_projects_lock = threading.Lock()


def _get_reuse_project(dir_path):
    """Return a cached reuse Project rooted at dir_path, building it if needed."""
    project = _reuse_projects.get(dir_path)
    if project is None:
        with _reuse_projects_lock:
            project = _reuse_projects.get(dir_path)
            if project is None:
                project = Project(dir_path)
                _reuse_projects[dir_path] = project
    return project


# ScannerConfig contains settings used to configure how the SBOM
# scanning should occur.
@dataclass(eq=True)
class ScannerConfig:
    # when assembling a Component's data, should we auto-conclude the
    # Component's license, based on the licenses of its Files?
    should_conclude_component_license: bool = True

    # when assembling a Component's Files' data, should we auto-conclude
    # each File's license, based on its detected license(s)?
    should_conclude_file_licenses: bool = True

    # number of lines to scan for SPDX-License-Identifier (0 = all)
    # defaults to 20
    num_lines_scanned: int = 20

    # should we calculate SHA256 hashes for each Component's Files?
    # note that SHA1 hashes are mandatory, per SPDX 2.3
    do_sha256: bool = True

    # should we calculate MD5 hashes for each Component's Files?
    do_md5: bool = False


def parse_line_for_expression(line):
    """Return parsed SPDX expression if tag found in line, or None otherwise."""
    p = line.partition("SPDX-License-Identifier:")
    if p[2] == "":
        return None
    # strip away trailing comment marks and whitespace, if any
    expression = p[2].strip()
    expression = expression.rstrip("/*")
    expression = expression.strip()
    return expression


def get_expression_data(file_path, num_lines):
    """
    Scans the specified file for the first SPDX-License-Identifier:
    tag in the file.

    Arguments:
        - file_path: path to file to scan.
        - num_lines: number of lines to scan for an expression before
                    giving up. If 0, will scan the entire file.
    Returns: parsed expression if found; None if not found.
    """
    _logger.debug("  - getting licenses for %s", file_path)

    with open(file_path) as f:
        try:
            for lineno, line in enumerate(f, start=1):
                if lineno > num_lines > 0:
                    break
                expression = parse_line_for_expression(line)
                if expression is not None:
                    return expression
        except UnicodeDecodeError:
            # invalid UTF-8 content
            return None

    # if we get here, we didn't find an expression
    return None


def split_expression(expression):
    """
    Parse a license expression into its constituent identifiers.

    Arguments:
        - expression: SPDX license expression
    Returns: array of split identifiers
    """
    # remove parens and plus sign
    e2 = re.sub(r'\(|\)|\+', "", expression, flags=re.IGNORECASE)

    # remove word operators, ignoring case, leaving a blank space
    e3 = re.sub(r' AND | OR | WITH ', " ", e2, flags=re.IGNORECASE)

    # and split on space
    e4 = e3.split(" ")

    return sorted(e4)


def check_license_valid(lic, sbom_graph):
    """
    Check whether this license ID is a valid SPDX license ID, and add it
    to the custom license IDs set for this SBOM if it isn't.

    Arguments:
        - lic: detected license ID
        - sbom_graph: SBOMGraph
    """
    if lic not in LICENSES:
        sbom_graph.custom_license_ids.add(lic)


def get_component_licenses(component):
    """
    Extract lists of all concluded and infoInFile licenses seen.

    Arguments:
        - component: SBOMComponent
    Returns: sorted list of concluded license exprs,
             sorted list of infoInFile ID's
    """
    lics_concluded = set()
    lics_from_files = set()
    for f in component.files.values():
        lics_concluded.add(f.concluded_license)
        for lic_info in f.license_info_in_file:
            lics_from_files.add(lic_info)
    return sorted(list(lics_concluded)), sorted(list(lics_from_files))


def normalize_expression(lics_concluded):
    """
    Combine array of license expressions into one AND'd expression,
    adding parens where needed.

    Arguments:
        - lics_concluded: array of license expressions
    Returns: string with single AND'd expression.
    """
    # return appropriate for simple cases
    if len(lics_concluded) == 0:
        return "NOASSERTION"
    if len(lics_concluded) == 1:
        return lics_concluded[0]

    # more than one, so we'll need to combine them
    # if and only if an expression has spaces, it needs parens
    revised = []
    for lic in lics_concluded:
        if lic in ["NONE", "NOASSERTION"]:
            continue
        if " " in lic:
            revised.append(f"({lic})")
        else:
            revised.append(lic)
    return " AND ".join(revised)


def get_copyright_info(file_path):
    """
    Scans the specified file for copyright information using REUSE tools.

    Arguments:
        - file_path: path to file to scan

    Returns: list of copyright statements if found; empty list if not found
    """
    _logger.debug("  - getting copyright info for %s", file_path)

    try:
        project = _get_reuse_project(os.path.dirname(file_path))
        infos = project.reuse_info_of(file_path)
        copyrights = []

        for info in infos:
            for notice in info.copyright_notices:
                copyrights.extend([notice.original])

        return copyrights
    except Exception:
        _logger.warning("Error getting copyright info for %s", file_path, exc_info=True)
        return []


def scan_file(cfg, f):
    """
    Gather hashes, license expression, and copyright info for a single File.

    Arguments:
        - cfg: ScannerConfig
        - f: SBOMFile to populate (mutated in place)
    """
    # set relpath based on the owning component's base_dir
    if f.component is not None and f.component.base_dir:
        f.relative_path = os.path.relpath(f.path, f.component.base_dir)

    # get hashes for file
    hashes = get_hashes(f.path)
    if not hashes:
        _logger.warning("unable to get hashes for file %s; skipping", f.path)
        return
    h_sha1, h_sha256, h_md5 = hashes
    f.hashes["SHA1"] = h_sha1
    if cfg.do_sha256:
        f.hashes["SHA256"] = h_sha256
    if cfg.do_md5:
        f.hashes["MD5"] = h_md5

    # get licenses for file
    expression = get_expression_data(f.path, cfg.num_lines_scanned)
    if expression:
        if cfg.should_conclude_file_licenses:
            f.concluded_license = expression
        f.license_info_in_file = split_expression(expression)

    if copyrights := get_copyright_info(f.path):
        f.copyright_text = "<text>\n" + "\n".join(copyrights) + "\n</text>"


def scan_sbom_graph(cfg, sbom_graph):
    """
    Scan for licenses and calculate hashes for all Files and Components
    in this SBOM graph.

    Arguments:
        - cfg: ScannerConfig
        - sbom_graph: SBOMGraph
    """
    # Files are globally unique by path in the graph, so scan them once each.
    # Hashing, license detection, and REUSE copyright lookups are independent
    # per file and I/O-bound, so fan them out across a thread pool rather than
    # grinding through thousands of files serially.
    files = list(sbom_graph.files.values())
    if files:
        _logger.info("scanning %d files", len(files))
        max_workers = min(32, (os.cpu_count() or 1) * 4)
        with ThreadPoolExecutor(max_workers=max_workers) as executor:
            for _ in tqdm.tqdm(
                executor.map(lambda f: scan_file(cfg, f), files),
                total=len(files),
                desc="Scanning files",
                unit="file",
            ):
                pass

    # collect custom license IDs and assemble per-Component data (cheap, and
    # touches shared graph state, so done serially after the parallel scan)
    for component in sbom_graph.components.values():
        for f in component.files.values():
            # check if any custom license IDs should be flagged for SBOM
            for lic in f.license_info_in_file:
                check_license_valid(lic, sbom_graph)

        lics_concluded, lics_from_files = get_component_licenses(component)
        if cfg.should_conclude_component_license:
            component.concluded_license = normalize_expression(lics_concluded)
        component.license_info_from_files = lics_from_files


# Backward compatibility alias (deprecated)
def scan_document(cfg, doc):
    """
    Deprecated: Use scan_sbom_graph instead.
    This function is kept for backward compatibility during migration.
    """
    # This would need to convert Document to SBOMGraph, but since we're
    # migrating away from Document, this should not be called in new code.
    raise NotImplementedError(
        "scan_document is deprecated. Use scan_sbom_graph with SBOMGraph instead."
    )
