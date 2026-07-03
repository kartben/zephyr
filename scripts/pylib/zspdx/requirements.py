# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Requirement traceability support for the SBOM.

This module bridges two sources of requirement data into the SBOM model:

* Doxygen ``@satisfies`` / ``@verifies`` commands embedded in source files.
  ``@satisfies`` marks an implementation (typically an API declaration in a
  header) that fulfills a requirement; ``@verifies`` marks a test that exercises
  one. Both reference a StrictDoc requirement UID such as ``ZEP-SRS-1-1``.

* The StrictDoc requirement catalog published by the ``reqmgmt`` module, which
  provides the human-readable statement/title for each UID.

Serializers consume this to emit SPDX 3.1 ``Requirement`` elements and connect
them to the files that implement or test them. Because ``@satisfies`` lives in
header files, the source-to-requirement trace only materializes when the SBOM
is generated with ``--analyze-includes`` (which brings headers into the graph).
"""

from __future__ import annotations

import logging
import os
import re
from dataclasses import dataclass

_logger = logging.getLogger(__name__)

# Doxygen requirement-trace commands, e.g. "@satisfies ZEP-SRS-1-1" or
# "\verifies ZEP-SYRS-15". The UID grammar matches the hyphen-separated
# StrictDoc UIDs used by the reqmgmt module.
_TAG_RE = re.compile(
    r"[@\\](satisfies|verifies)\s+([A-Za-z][A-Za-z0-9]*(?:-[A-Za-z0-9]+)+)"
)

# File extensions worth scanning for requirement tags (C-family sources/headers).
_SOURCE_EXTS = frozenset(
    {".c", ".h", ".cpp", ".hpp", ".cc", ".cxx", ".hxx", ".S", ".s", ".inc"}
)

# Candidate locations of the reqmgmt module, relative to ZEPHYR_BASE, tried when
# the module directory is not given explicitly or via the CMake cache.
_CANDIDATE_RELPATHS = (
    ("..", "doc", "reqmgmt"),
    ("..", "reqmgmt"),
    ("..", "tools", "reqmgmt"),
    ("..", "modules", "lib", "reqmgmt"),
)


@dataclass
class RequirementInfo:
    """A single requirement, as parsed from the StrictDoc catalog."""

    uid: str
    title: str = ""
    statement: str = ""
    status: str = ""
    rtype: str = ""
    component: str = ""
    rationale: str = ""


def should_scan_for_requirements(file_path: str) -> bool:
    """Whether a file is a C-family source worth scanning for requirement tags."""
    return os.path.splitext(file_path)[1] in _SOURCE_EXTS


def get_requirement_tags(file_path: str) -> dict[str, list[str]]:
    """Return the requirement UIDs referenced by a source file.

    Scans the whole file (tags may appear deep in it) for Doxygen ``@satisfies``
    and ``@verifies`` commands; the backslash forms ``\\satisfies`` /
    ``\\verifies`` are also accepted.

    Args:
        file_path: Absolute path to the file to scan.

    Returns:
        Mapping with ``"satisfies"`` and ``"verifies"`` keys, each a sorted list
        of unique requirement UIDs (empty when none are found).
    """
    satisfies: set[str] = set()
    verifies: set[str] = set()
    try:
        with open(file_path, encoding="utf-8", errors="ignore") as f:
            for line in f:
                # Cheap prefilter before running the regex on every line.
                if "satisfies" not in line and "verifies" not in line:
                    continue
                for kind, uid in _TAG_RE.findall(line):
                    (satisfies if kind == "satisfies" else verifies).add(uid)
    except OSError as e:
        _logger.warning("could not read %s for requirement tags: %s", file_path, e)
    return {"satisfies": sorted(satisfies), "verifies": sorted(verifies)}


def _read_cache_vars(build_dir: str, names: set[str]) -> dict[str, str]:
    """Read selected ``NAME:TYPE=VALUE`` entries from a build's CMakeCache.txt."""
    result: dict[str, str] = {}
    cache = os.path.join(build_dir or "", "CMakeCache.txt")
    if not os.path.isfile(cache):
        return result
    try:
        with open(cache, encoding="utf-8", errors="ignore") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith(("#", "//")):
                    continue
                key, sep, value = line.partition("=")
                if not sep:
                    continue
                name = key.split(":", 1)[0]
                if name in names:
                    result[name] = value.strip()
    except OSError as e:
        _logger.warning("could not read CMake cache %s: %s", cache, e)
    return result


def _is_reqmgmt_dir(path: str) -> bool:
    """Whether ``path`` looks like the reqmgmt StrictDoc requirements module."""
    if not path or not os.path.isdir(path):
        return False
    if os.path.isfile(os.path.join(path, "strictdoc.toml")):
        return True
    return os.path.isdir(os.path.join(path, "docs", "software_requirements"))


def find_requirements_dir(build_dir: str = "", explicit: str = "") -> str | None:
    """Locate the reqmgmt module directory.

    Resolution order: an explicit path, the ``ZEPHYR_REQMGMT_MODULE_DIR``
    environment variable, the same variable from the build's CMake cache, and
    finally a small set of well-known locations relative to ``ZEPHYR_BASE``.

    Args:
        build_dir: Build directory whose CMakeCache.txt may record the module.
        explicit: Caller-provided path (e.g. from ``--requirements-dir``).

    Returns:
        Absolute path to the module, or ``None`` if it cannot be found.
    """
    candidates: list[str] = []
    if explicit:
        candidates.append(os.path.expanduser(explicit))
    if env := os.environ.get("ZEPHYR_REQMGMT_MODULE_DIR"):
        candidates.append(os.path.expanduser(env))

    cache = _read_cache_vars(build_dir, {"ZEPHYR_REQMGMT_MODULE_DIR", "ZEPHYR_BASE"})
    if cached := cache.get("ZEPHYR_REQMGMT_MODULE_DIR"):
        candidates.append(os.path.expanduser(cached))
    if zephyr_base := cache.get("ZEPHYR_BASE"):
        candidates.extend(os.path.join(zephyr_base, *rel) for rel in _CANDIDATE_RELPATHS)

    for candidate in candidates:
        if _is_reqmgmt_dir(candidate):
            return os.path.abspath(candidate)
    return None


def _parse_sdoc(path: str) -> dict[str, RequirementInfo]:
    """Parse one StrictDoc ``.sdoc`` file into a ``{uid: RequirementInfo}`` map.

    Only ``[REQUIREMENT]`` sections are considered. Both single-line fields
    (``TITLE: ...``) and StrictDoc multi-line fields (``STATEMENT: >>>`` … ``<<<``)
    are handled; unrelated fields (relations, etc.) are ignored.
    """
    reqs: dict[str, RequirementInfo] = {}
    try:
        with open(path, encoding="utf-8", errors="ignore") as f:
            lines = f.read().splitlines()
    except OSError as e:
        _logger.warning("could not read requirement file %s: %s", path, e)
        return reqs

    field_map = {
        "UID": "uid",
        "TITLE": "title",
        "STATEMENT": "statement",
        "STATUS": "status",
        "TYPE": "rtype",
        "COMPONENT": "component",
        "RATIONALE": "rationale",
    }

    def flush(block: dict[str, str] | None) -> None:
        if block and block.get("uid"):
            reqs[block["uid"]] = RequirementInfo(**block)

    current: dict[str, str] | None = None
    i, n = 0, len(lines)
    while i < n:
        line = lines[i]
        if line.strip().startswith("["):
            # Section boundary: finalize the previous requirement, and start a
            # fresh block only when we are entering a [REQUIREMENT] section.
            flush(current)
            current = {} if line.strip() == "[REQUIREMENT]" else None
            i += 1
            continue
        if current is None:
            i += 1
            continue
        # Only top-level "KEY: value" lines carry fields; indented list items
        # (relations) start with whitespace or '-' and are skipped.
        m = re.match(r"([A-Z_]+): ?(.*)$", line)
        if m and not line[:1].isspace() and not line.startswith("-"):
            attr = field_map.get(m.group(1))
            value = m.group(2)
            if value == ">>>":
                buf: list[str] = []
                i += 1
                while i < n and lines[i].strip() != "<<<":
                    buf.append(lines[i])
                    i += 1
                value = "\n".join(buf).strip()
            if attr:
                current[attr] = value.strip()
        i += 1
    flush(current)
    return reqs


def load_requirements_catalog(
    build_dir: str = "", explicit: str = ""
) -> dict[str, RequirementInfo]:
    """Locate the reqmgmt module and parse its StrictDoc requirement catalog.

    Args:
        build_dir: Build directory used to locate the module via CMake cache.
        explicit: Caller-provided module path, if any.

    Returns:
        Mapping of requirement UID to :class:`RequirementInfo`; empty if the
        module cannot be found or read.
    """
    req_dir = find_requirements_dir(build_dir, explicit)
    if not req_dir:
        _logger.info(
            "requirements: reqmgmt module not found; requirement statements unavailable"
        )
        return {}

    catalog: dict[str, RequirementInfo] = {}
    docs_dir = os.path.join(req_dir, "docs")
    search_root = docs_dir if os.path.isdir(docs_dir) else req_dir
    for root, _dirs, files in os.walk(search_root):
        for filename in files:
            if filename.endswith(".sdoc"):
                catalog.update(_parse_sdoc(os.path.join(root, filename)))

    _logger.info("requirements: loaded %d requirement(s) from %s", len(catalog), req_dir)
    return catalog
