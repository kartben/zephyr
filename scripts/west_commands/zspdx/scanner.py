# Copyright (c) 2020, 2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import hashlib
import os
import re
from dataclasses import dataclass

from reuse.project import Project
from west import log

from zspdx.licenses import LICENSES
from zspdx.util import getHashes


# ScannerConfig contains settings used to configure how the SBOM
# scanning should occur.
@dataclass(eq=True)
class ScannerConfig:
    # when assembling a Component's data, should we auto-conclude the
    # Component's license, based on the licenses of its Files?
    shouldConcludeComponentLicense: bool = True

    # when assembling a Component's Files' data, should we auto-conclude
    # each File's license, based on its detected license(s)?
    shouldConcludeFileLicenses: bool = True

    # number of lines to scan for SPDX-License-Identifier (0 = all)
    # defaults to 20
    numLinesScanned: int = 20

    # should we calculate SHA256 hashes for each Component's Files?
    # note that SHA1 hashes are mandatory, per SPDX 2.3
    doSHA256: bool = True

    # should we calculate MD5 hashes for each Component's Files?
    doMD5: bool = False


def parseLineForExpression(line):
    """Return parsed SPDX expression if tag found in line, or None otherwise."""
    p = line.partition("SPDX-License-Identifier:")
    if p[2] == "":
        return None
    # strip away trailing comment marks and whitespace, if any
    expression = p[2].strip()
    expression = expression.rstrip("/*")
    expression = expression.strip()
    return expression


def getExpressionData(filePath, numLines):
    """
    Scans the specified file for the first SPDX-License-Identifier:
    tag in the file.

    Arguments:
        - filePath: path to file to scan.
        - numLines: number of lines to scan for an expression before
                    giving up. If 0, will scan the entire file.
    Returns: parsed expression if found; None if not found.
    """
    log.dbg(f"  - getting licenses for {filePath}")

    with open(filePath) as f:
        try:
            for lineno, line in enumerate(f, start=1):
                if lineno > numLines > 0:
                    break
                expression = parseLineForExpression(line)
                if expression is not None:
                    return expression
        except UnicodeDecodeError:
            # invalid UTF-8 content
            return None

    # if we get here, we didn't find an expression
    return None


def splitExpression(expression):
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


def calculateVerificationCode(component):
    """
    Calculate the SPDX Package Verification Code for all files in the component.

    Arguments:
        - component: SBOMComponent
    Returns: verification code as string
    """
    hashes = []
    for f in component.files.values():
        if "SHA1" in f.hashes:
            hashes.append(f.hashes["SHA1"])
    hashes.sort()
    filelist = "".join(hashes)

    hSHA1 = hashlib.sha1(usedforsecurity=False)
    hSHA1.update(filelist.encode('utf-8'))
    return hSHA1.hexdigest()


def checkLicenseValid(lic, sbom_data):
    """
    Check whether this license ID is a valid SPDX license ID, and add it
    to the custom license IDs set for this SBOM if it isn't.

    Arguments:
        - lic: detected license ID
        - sbom_data: SBOMData
    """
    if lic not in LICENSES:
        sbom_data.custom_license_ids.add(lic)


def getComponentLicenses(component):
    """
    Extract lists of all concluded and infoInFile licenses seen.

    Arguments:
        - component: SBOMComponent
    Returns: sorted list of concluded license exprs,
             sorted list of infoInFile ID's
    """
    licsConcluded = set()
    licsFromFiles = set()
    for f in component.files.values():
        licsConcluded.add(f.concluded_license)
        for licInfo in f.license_info_in_file:
            licsFromFiles.add(licInfo)
    return sorted(list(licsConcluded)), sorted(list(licsFromFiles))


def normalizeExpression(licsConcluded):
    """
    Combine array of license expressions into one AND'd expression,
    adding parens where needed.

    Arguments:
        - licsConcluded: array of license expressions
    Returns: string with single AND'd expression.
    """
    # return appropriate for simple cases
    if len(licsConcluded) == 0:
        return "NOASSERTION"
    if len(licsConcluded) == 1:
        return licsConcluded[0]

    # more than one, so we'll need to combine them
    # if and only if an expression has spaces, it needs parens
    revised = []
    for lic in licsConcluded:
        if lic in ["NONE", "NOASSERTION"]:
            continue
        if " " in lic:
            revised.append(f"({lic})")
        else:
            revised.append(lic)
    return " AND ".join(revised)


def getCopyrightInfo(filePath):
    """
    Scans the specified file for copyright information using REUSE tools.

    Arguments:
        - filePath: path to file to scan

    Returns: list of copyright statements if found; empty list if not found
    """
    log.dbg(f"  - getting copyright info for {filePath}")

    try:
        project = Project(os.path.dirname(filePath))
        infos = project.reuse_info_of(filePath)
        copyrights = []

        for info in infos:
            for notice in info.copyright_notices:
                copyrights.extend([notice.original])

        return copyrights
    except Exception as e:
        log.wrn(f"Error getting copyright info for {filePath}: {e}")
        return []


def scanSBOMData(cfg, sbom_data):
    """
    Scan for licenses and calculate hashes for all Files and Components
    in this SBOM data.

    Arguments:
        - cfg: ScannerConfig
        - sbom_data: SBOMData
    """
    for component in sbom_data.components.values():
        log.inf(f"scanning files in component {component.name}")

        # first, gather File data for this component
        for f in component.files.values():
            # set relpath based on component's base_dir
            f.relative_path = os.path.relpath(f.path, component.base_dir)

            # get hashes for file
            hashes = getHashes(f.path)
            if not hashes:
                log.wrn(f"unable to get hashes for file {f.path}; skipping")
                continue
            hSHA1, hSHA256, hMD5 = hashes
            f.hashes["SHA1"] = hSHA1
            if cfg.doSHA256:
                f.hashes["SHA256"] = hSHA256
            if cfg.doMD5:
                f.hashes["MD5"] = hMD5

            # get licenses for file
            expression = getExpressionData(f.path, cfg.numLinesScanned)
            if expression:
                if cfg.shouldConcludeFileLicenses:
                    f.concluded_license = expression
                f.license_info_in_file = splitExpression(expression)

            if copyrights := getCopyrightInfo(f.path):
                f.copyright_text = "<text>\n" + "\n".join(copyrights) + "\n</text>"

            # check if any custom license IDs should be flagged for SBOM
            for lic in f.license_info_in_file:
                checkLicenseValid(lic, sbom_data)

        # now, assemble the Component data
        licsConcluded, licsFromFiles = getComponentLicenses(component)
        if cfg.shouldConcludeComponentLicense:
            component.concluded_license = normalizeExpression(licsConcluded)
        component.license_info_from_files = licsFromFiles
        component.verification_code = calculateVerificationCode(component)
