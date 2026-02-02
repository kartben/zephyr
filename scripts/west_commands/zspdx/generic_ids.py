# Copyright (c) 2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
Generic ID generation utilities for SBOM elements.
This module provides format-agnostic ID generation that can be adapted
to any SBOM format (SPDX, CycloneDX, etc.).
"""

import re


def getSafeCharacter(c):
    """
    Converts a character to a safe character for IDs.

    Arguments:
        - c: character to test
    Returns: c if it is safe (letter, number, '-' or '.');
             '-' otherwise
    """
    if c.isalpha() or c.isdigit() or c == "-" or c == ".":
        return c
    return "-"


def convertToSafe(s):
    """
    Converts a filename or other string to only safe characters for IDs.
    Note that a separate check (such as in getUniqueID, below) will need
    to be used to confirm that this is still a unique identifier, after
    conversion.

    Arguments:
        - s: string to be converted.
    Returns: string with all non-safe characters replaced with dashes.
    """
    return "".join([getSafeCharacter(c) for c in s])


def getUniqueFileID(filenameOnly, timesSeen, prefix="File"):
    """
    Find a unique ID for a file among others seen so far.

    Arguments:
        - filenameOnly: filename only (directories omitted) seeking ID.
        - timesSeen: dict of all filename-only to number of times seen.
        - prefix: Optional prefix for the ID (default: "File")
    Returns: unique ID; updates timesSeen to include it.
    """

    converted = convertToSafe(filenameOnly)
    fileID = f"{prefix}-{converted}" if prefix else converted

    # determine whether fileID is unique so far, or not
    filenameTimesSeen = timesSeen.get(converted, 0) + 1
    if filenameTimesSeen > 1:
        # we'll append the # of times seen to the end
        fileID += f"-{filenameTimesSeen}"
    else:
        # first time seeing this filename
        # edge case: if the filename itself ends in "-{number}", then we
        # need to add a "-1" to it, so that we don't end up overlapping
        # with an appended number from a similarly-named file.
        p = re.compile(r"-\d+$")
        if p.search(converted):
            fileID += "-1"

    timesSeen[converted] = filenameTimesSeen
    return fileID


def getUniquePackageID(packageName, prefix="Package"):
    """
    Generate a unique ID for a package.

    Arguments:
        - packageName: Name of the package
        - prefix: Optional prefix for the ID (default: "Package")
    Returns: unique ID for the package
    """
    converted = convertToSafe(packageName)
    if prefix:
        return f"{prefix}-{converted}"
    return converted


def getUniqueDocumentID(documentName):
    """
    Generate a unique ID for a document.

    Arguments:
        - documentName: Name of the document
    Returns: unique ID for the document
    """
    converted = convertToSafe(documentName)
    return f"Document-{converted}"
