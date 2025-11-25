# Copyright (c) 2020, 2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
SPDX document writer - main entry point.

This module provides the unified writeSPDX() function that dispatches to
the appropriate version-specific writer (SPDX 2.x or SPDX 3.x).

For SPDX 2.x (tag-value format), see zspdx.spdx2.writer
For SPDX 3.x (JSON-LD format), see zspdx.spdx3.writer
"""

from zspdx.spdx2 import write_spdx as write_spdx2
from zspdx.spdx3 import write_spdx as write_spdx3
from zspdx.version import SPDX_VERSION_2_3, SPDX_VERSION_3_0


def writeSPDX(spdxPath, doc, spdx_version=SPDX_VERSION_2_3):
    """
    Open SPDX document file for writing, write the document, and calculate
    its hash for other referring documents to use.

    This function dispatches to the appropriate version-specific writer:
    - SPDX 2.x: Tag-value format (zspdx.spdx2.writer)
    - SPDX 3.x: JSON-LD format (zspdx.spdx3.writer)

    Arguments:
        spdxPath: path to write SPDX document
        doc: SPDX Document object to write
        spdx_version: SPDX specification version (default: 2.3)
    Returns: True on success, False on failure
    """
    if spdx_version >= SPDX_VERSION_3_0:
        return write_spdx3(spdxPath, doc)
    else:
        return write_spdx2(spdxPath, doc, spdx_version)

