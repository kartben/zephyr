# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
SBOM File model.

This module defines the SBOMFile class, which represents an individual file
within a software package. Files are the atomic units tracked in an SBOM.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import TYPE_CHECKING

from zspdx.model.base import ElementType, SBOMElement

if TYPE_CHECKING:
    from zspdx.model.relationship import SBOMRelationship


@dataclass
class FileHashes:
    """
    Cryptographic hashes for a file.

    Multiple hash algorithms are supported to accommodate different SBOM
    format requirements and security practices. SHA-1 is typically required
    for SPDX compatibility, while SHA-256 is recommended for security.

    Attributes:
        sha1: SHA-1 hash of the file content (160-bit, 40 hex chars).
            Required for SPDX verification codes and file identification.
        sha256: SHA-256 hash of the file content (256-bit, 64 hex chars).
            Recommended for security-sensitive applications.
            May be empty if not computed.
        md5: MD5 hash of the file content (128-bit, 32 hex chars).
            Legacy support; generally not recommended for security use.
            May be empty if not computed.

    Example:
        >>> hashes = FileHashes(
        ...     sha1="da39a3ee5e6b4b0d3255bfef95601890afd80709",
        ...     sha256="e3b0c44298fc1c149afbf4c8996fb924..."
        ... )
        >>> bool(hashes.sha1)
        True
        >>> bool(hashes.md5)
        False

    Note:
        All hash values should be lowercase hexadecimal strings.
        Empty strings indicate the hash was not computed.
    """

    sha1: str = ""
    sha256: str = ""
    md5: str = ""


@dataclass
class SBOMFile(SBOMElement):
    """
    Represents a file in the SBOM.

    Files are the atomic units tracked in an SBOM. They represent actual
    files on disk with their metadata, cryptographic hashes, and license
    information extracted from file contents.

    Files belong to packages and can have relationships with other elements
    (typically indicating derivation or dependency relationships).

    Attributes:
        absolute_path: Absolute filesystem path to this file.
            Used for file operations during scanning and for internal lookups.
        relative_path: Path relative to the owning package's relative_base_dir.
            This is what appears in the serialized SBOM output.
        hashes: Cryptographic hashes of the file content.
            Populated during scanning.
        concluded_license: License concluded from analysis of file content.
            Derived from SPDX-License-Identifier tags or other analysis.
            Defaults to "NOASSERTION" if no license was detected.
        license_info_in_file: List of license identifiers detected in the file.
            These are the individual licenses found, before being combined
            into a concluded license expression.
        copyright_text: Copyright statements found in the file.
            May be a multi-line string for files with multiple notices.
            Defaults to "NOASSERTION" if no copyright was detected.
        relationships: List of relationships owned by this file.
            The file is the source (left side) of these relationships.
            Common relationship types: GENERATED_FROM (for build outputs).

    Example:
        >>> from zspdx.model import SBOMFile, FileHashes
        >>> src_file = SBOMFile(
        ...     internal_id="main-c",
        ...     name="main.c",
        ...     absolute_path="/home/user/project/src/main.c",
        ...     relative_path="src/main.c"
        ... )
        >>> src_file.hashes = FileHashes(
        ...     sha1="abc123...",
        ...     sha256="def456..."
        ... )
        >>> src_file.concluded_license = "Apache-2.0"
        >>> src_file.license_info_in_file = ["Apache-2.0"]

    Note:
        The hashes, license information, and copyright text are typically
        populated by the scanner, not during initial file creation.
    """

    absolute_path: str = ""
    relative_path: str = ""
    hashes: FileHashes = field(default_factory=FileHashes)
    concluded_license: str = "NOASSERTION"
    license_info_in_file: list[str] = field(default_factory=list)
    copyright_text: str = "NOASSERTION"
    relationships: list[SBOMRelationship] = field(default_factory=list)

    @property
    def element_type(self) -> ElementType:
        """Return ElementType.FILE."""
        return ElementType.FILE
