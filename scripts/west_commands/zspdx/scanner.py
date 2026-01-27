# Copyright (c) 2020, 2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
SBOM file scanner for license detection and hash calculation.

This module provides functions to scan files for licenses, calculate
cryptographic hashes, and extract copyright information.
"""

from __future__ import annotations

import hashlib
import os
import re
from dataclasses import dataclass
from typing import TYPE_CHECKING

from reuse.project import Project
from west import log

from zspdx.licenses import LICENSES
from zspdx.model.file import FileHashes
from zspdx.util import getHashes

if TYPE_CHECKING:
    from zspdx.model.document import SBOMDocument


@dataclass(eq=True)
class ScannerConfig:
    """
    Configuration for SPDX Document scanning.

    Attributes:
        shouldConcludePackageLicense: When assembling a Package's data,
            should we auto-conclude the Package's license based on the
            licenses of its Files?
        shouldConcludeFileLicenses: When assembling a Package's Files' data,
            should we auto-conclude each File's license based on its
            detected license(s)?
        numLinesScanned: Number of lines to scan for SPDX-License-Identifier
            (0 = scan all lines). Defaults to 20.
        doSHA256: Should we calculate SHA256 hashes for each Package's Files?
            Note that SHA1 hashes are mandatory per SPDX 2.3.
        doMD5: Should we calculate MD5 hashes for each Package's Files?
    """

    shouldConcludePackageLicense: bool = True
    shouldConcludeFileLicenses: bool = True
    numLinesScanned: int = 20
    doSHA256: bool = True
    doMD5: bool = False


def parseLineForExpression(line: str) -> str | None:
    """
    Parse a line for an SPDX license expression.

    Args:
        line: A single line of text to check.

    Returns:
        Parsed SPDX expression if tag found, None otherwise.
    """
    p = line.partition("SPDX-License-Identifier:")
    if p[2] == "":
        return None
    # Strip away trailing comment marks and whitespace
    expression = p[2].strip()
    expression = expression.rstrip("/*")
    expression = expression.strip()
    return expression


def getExpressionData(filePath: str, numLines: int) -> str | None:
    """
    Scan a file for the first SPDX-License-Identifier tag.

    Args:
        filePath: Path to file to scan.
        numLines: Number of lines to scan for an expression before
            giving up. If 0, will scan the entire file.

    Returns:
        Parsed expression if found, None if not found.
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
            # Invalid UTF-8 content
            return None

    # Didn't find an expression
    return None


def splitExpression(expression: str) -> list[str]:
    """
    Parse a license expression into its constituent identifiers.

    Args:
        expression: SPDX license expression.

    Returns:
        Sorted array of split identifiers.
    """
    # Remove parens and plus sign
    e2 = re.sub(r"\(|\)|\+", "", expression, flags=re.IGNORECASE)

    # Remove word operators, ignoring case, leaving a blank space
    e3 = re.sub(r" AND | OR | WITH ", " ", e2, flags=re.IGNORECASE)

    # Split on space
    e4 = e3.split(" ")

    return sorted(e4)


def calculateVerificationCode(pkg) -> str:
    """
    Calculate the SPDX Package Verification Code for all files in the package.

    This is calculated per section 7.9 of SPDX spec v2.3.

    Args:
        pkg: SBOMPackage to calculate verification code for.

    Returns:
        Verification code as hex string.
    """
    hashes = []
    for f in pkg.files.values():
        hashes.append(f.hashes.sha1)
    hashes.sort()
    filelist = "".join(hashes)

    hSHA1 = hashlib.sha1(usedforsecurity=False)
    hSHA1.update(filelist.encode("utf-8"))
    return hSHA1.hexdigest()


def checkLicenseValid(lic: str, doc: SBOMDocument) -> None:
    """
    Check if a license ID is a valid SPDX license ID.

    If the license is not in the standard SPDX license list, it is added
    to the document's custom license IDs set.

    Args:
        lic: Detected license ID.
        doc: SBOMDocument to add custom license to if needed.
    """
    if lic not in LICENSES:
        doc.custom_license_ids.add(lic)


def getPackageLicenses(pkg) -> tuple[list[str], list[str]]:
    """
    Extract lists of all concluded and infoInFile licenses seen.

    Args:
        pkg: SBOMPackage to extract licenses from.

    Returns:
        Tuple of (sorted concluded license expressions,
                  sorted infoInFile IDs).
    """
    licsConcluded = set()
    licsFromFiles = set()
    for f in pkg.files.values():
        licsConcluded.add(f.concluded_license)
        for licInfo in f.license_info_in_file:
            licsFromFiles.add(licInfo)
    return sorted(list(licsConcluded)), sorted(list(licsFromFiles))


def normalizeExpression(licsConcluded: list[str]) -> str:
    """
    Combine array of license expressions into one AND'd expression.

    Adds parentheses where needed for expressions containing spaces.

    Args:
        licsConcluded: Array of license expressions.

    Returns:
        Single AND'd expression string.
    """
    # Return appropriate for simple cases
    if len(licsConcluded) == 0:
        return "NOASSERTION"
    if len(licsConcluded) == 1:
        return licsConcluded[0]

    # More than one, so we'll need to combine them
    # If and only if an expression has spaces, it needs parens
    revised = []
    for lic in licsConcluded:
        if lic in ["NONE", "NOASSERTION"]:
            continue
        if " " in lic:
            revised.append(f"({lic})")
        else:
            revised.append(lic)
    return " AND ".join(revised)


def getCopyrightInfo(filePath: str) -> list[str]:
    """
    Scan a file for copyright information using REUSE tools.

    Args:
        filePath: Path to file to scan.

    Returns:
        List of copyright statements if found, empty list if not found.
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


def scanDocument(cfg: ScannerConfig, doc: SBOMDocument) -> None:
    """
    Scan for licenses and calculate hashes for all Files and Packages.

    This function populates file hashes, license information, and copyright
    text for all files in the document. It also calculates package-level
    verification codes and concluded licenses.

    Args:
        cfg: ScannerConfig with scan settings.
        doc: SBOMDocument to scan.
    """
    for pkg in doc.packages.values():
        log.inf(f"scanning files in package {pkg.name} in document {doc.name}")

        # First, gather File data for this package
        for f in pkg.files.values():
            # Set relpath based on package's relativeBaseDir
            f.relative_path = os.path.relpath(f.absolute_path, pkg.relative_base_dir)

            # Get hashes for file
            hashes = getHashes(f.absolute_path)
            if not hashes:
                log.wrn(f"unable to get hashes for file {f.absolute_path}; skipping")
                continue
            hSHA1, hSHA256, hMD5 = hashes
            f.hashes = FileHashes(
                sha1=hSHA1,
                sha256=hSHA256 if cfg.doSHA256 else "",
                md5=hMD5 if cfg.doMD5 else "",
            )

            # Get licenses for file
            expression = getExpressionData(f.absolute_path, cfg.numLinesScanned)
            if expression:
                if cfg.shouldConcludeFileLicenses:
                    f.concluded_license = expression
                f.license_info_in_file = splitExpression(expression)

            if copyrights := getCopyrightInfo(f.absolute_path):
                f.copyright_text = "<text>\n" + "\n".join(copyrights) + "\n</text>"

            # Check if any custom license IDs should be flagged for document
            for lic in f.license_info_in_file:
                checkLicenseValid(lic, doc)

        # Now, assemble the Package data
        licsConcluded, licsFromFiles = getPackageLicenses(pkg)
        if cfg.shouldConcludePackageLicense:
            pkg.concluded_license = normalizeExpression(licsConcluded)
        pkg.license_info_from_files = licsFromFiles
        pkg.verification_code = calculateVerificationCode(pkg)
