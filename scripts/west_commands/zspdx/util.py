# Copyright (c) 2020, 2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import hashlib
import re

from west import log

# Regex patterns for external references
CPE23TYPE_REGEX = (
    r'^cpe:2\.3:[aho\*\-](:(((\?*|\*?)([a-zA-Z0-9\-\._]|(\\[\\\*\?!"#$$%&\'\(\)\+,\/:;<=>@\[\]\^'
    r"`\{\|}~]))+(\?*|\*?))|[\*\-])){5}(:(([a-zA-Z]{2,3}(-([a-zA-Z]{2}|[0-9]{3}))?)|[\*\-]))(:(((\?*"
    r'|\*?)([a-zA-Z0-9\-\._]|(\\[\\\*\?!"#$$%&\'\(\)\+,\/:;<=>@\[\]\^`\{\|}~]))+(\?*|\*?))|[\*\-])){4}$'
)
PURL_REGEX = r"^pkg:.+(\/.+)?\/.+(@.+)?(\?.+)?(#.+)?$"


def normalize_spdx_name(name):
    """
    Normalize a name for use in SPDX IDs.

    Replaces underscores with hyphens since underscores are not allowed in SPDX IDs.

    Arguments:
        - name: the name to normalize
    Returns: normalized name with underscores replaced by hyphens
    """
    return name.replace("_", "-")


def generate_download_url(url, revision):
    """
    Generate a download URL with revision information.

    Only git is supported. walker.py only parses revision if it's from a git repository.

    Arguments:
        - url: the base URL
        - revision: the git revision (commit, tag, branch)
    Returns: formatted download URL with revision, or just URL if no revision
    """
    if len(revision) == 0:
        return url
    return f'git+{url}@{revision}'


def is_cpe23_reference(ref):
    """Check if an external reference is a CPE 2.3 reference."""
    return bool(re.fullmatch(CPE23TYPE_REGEX, ref))


def is_purl_reference(ref):
    """Check if an external reference is a Package URL reference."""
    return bool(re.fullmatch(PURL_REGEX, ref))


def getHashes(filePath):
    """
    Scan for and return hashes.

    Arguments:
        - filePath: path to file to scan.
    Returns: tuple of (SHA1, SHA256, MD5) hashes for filePath, or
             None if file is not found.
    """
    hSHA1 = hashlib.sha1(usedforsecurity=False)
    hSHA256 = hashlib.sha256()
    hMD5 = hashlib.md5(usedforsecurity=False)

    log.dbg(f"  - getting hashes for {filePath}")

    try:
        with open(filePath, 'rb') as f:
            buf = f.read()
            hSHA1.update(buf)
            hSHA256.update(buf)
            hMD5.update(buf)
    except OSError:
        return None

    return (hSHA1.hexdigest(), hSHA256.hexdigest(), hMD5.hexdigest())
