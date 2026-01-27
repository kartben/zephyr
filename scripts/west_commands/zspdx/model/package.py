# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
SBOM Package model.

This module defines the SBOMPackage class, which represents a software package
or component in the SBOM. Packages are collections of files that together form
a distributable unit of software.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum, auto
from typing import TYPE_CHECKING

from zspdx.model.base import ElementType, SBOMElement

if TYPE_CHECKING:
    from zspdx.model.file import SBOMFile
    from zspdx.model.relationship import SBOMRelationship


class PackagePurpose(Enum):
    """
    Purpose or type classification of a software package.

    These are logical purpose categories that can be mapped to format-specific
    values during serialization. For example, SPDX 2.3 has PrimaryPackagePurpose
    while CycloneDX has component types.

    Attributes:
        APPLICATION: A standalone application or executable.
        LIBRARY: A reusable library or module.
        SOURCE: Source code (not compiled).
        FRAMEWORK: A software framework.
        CONTAINER: A container image.
        FIRMWARE: Firmware or embedded software.
        OPERATING_SYSTEM: An operating system or kernel.
        DEVICE: A hardware device driver.
        FILE: A single file (when treated as a package).
        OTHER: Any other type not covered above.
    """

    APPLICATION = auto()
    LIBRARY = auto()
    SOURCE = auto()
    FRAMEWORK = auto()
    CONTAINER = auto()
    FIRMWARE = auto()
    OPERATING_SYSTEM = auto()
    DEVICE = auto()
    FILE = auto()
    OTHER = auto()


@dataclass
class ExternalReference:
    """
    External reference for a package (CPE, PURL, etc.).

    External references provide links to external systems that can identify
    or provide more information about a package. Common types include
    security identifiers (CPE) and package manager locators (PURL).

    Attributes:
        ref_type: Type of reference. Common values:
            - "cpe23": CPE 2.3 formatted security identifier
            - "purl": Package URL for package manager identification
            - "url": Generic URL reference
            - "security-advisory": Link to security advisory
        locator: The actual reference value. Format depends on ref_type:
            - For cpe23: "cpe:2.3:a:vendor:product:version:..."
            - For purl: "pkg:type/namespace/name@version?qualifiers#subpath"
            - For url: A standard URL
        comment: Optional comment explaining this reference.

    Example:
        >>> # CPE reference for mbedTLS
        >>> cpe_ref = ExternalReference(
        ...     ref_type="cpe23",
        ...     locator="cpe:2.3:a:arm:mbed_tls:3.5.1:*:*:*:*:*:*:*"
        ... )
        >>> # PURL reference
        >>> purl_ref = ExternalReference(
        ...     ref_type="purl",
        ...     locator="pkg:github/zephyrproject-rtos/zephyr@v3.5.0"
        ... )
    """

    ref_type: str = ""
    locator: str = ""
    comment: str = ""


@dataclass
class SBOMPackage(SBOMElement):
    """
    Represents a software package in the SBOM.

    A package is a collection of files that together form a distributable
    unit of software. Packages can represent libraries, applications,
    source code collections, firmware components, etc.

    Packages contain files and can have relationships with other elements.
    They track license information both as declared by maintainers and as
    concluded from analysis of contained files.

    Attributes:
        version: Package version string (e.g., "3.5.0", "1.0.0-rc1").
        supplier: Package supplier or vendor name.
        download_url: URL where the package can be downloaded.
            For git repositories, this is typically the clone URL.
        revision: Source control revision (e.g., git commit hash, tag).
            Used to identify the exact version of source code.
        purpose: The primary purpose of this package. See PackagePurpose.
            May be None if purpose is unknown or not applicable.
        declared_license: License declared by the package maintainer.
            This is what the package claims its license is.
            Defaults to "NOASSERTION" if unknown.
        concluded_license: License concluded from file analysis.
            This is what analysis determined the license to be.
            Defaults to "NOASSERTION" if analysis was not performed.
        copyright_text: Copyright statement for the package.
            Defaults to "NOASSERTION" if unknown.
        external_references: List of external identifiers (CPE, PURL, etc.).
            These help identify the package in external systems.
        files: Dict mapping internal file IDs to SBOMFile objects.
            Keys are the file's internal_id for efficient lookup.
        relationships: List of relationships owned by this package.
            The package is the source (left side) of these relationships.
        verification_code: Package verification code computed from file hashes.
            For SPDX, this is computed per section 7.9 of the spec.
            Computed during scanning, empty until then.
        license_info_from_files: Aggregated list of licenses found in files.
            Populated during scanning based on file analysis.
        relative_base_dir: Absolute path of the base directory for this package.
            File relative paths are computed relative to this directory.
        primary_build_file: If this package represents a build target,
            this points to its main build output file.

    Example:
        >>> from zspdx.model import SBOMPackage, PackagePurpose, ExternalReference
        >>> pkg = SBOMPackage(
        ...     internal_id="zephyr-kernel",
        ...     name="zephyr",
        ...     version="3.5.0",
        ...     purpose=PackagePurpose.OPERATING_SYSTEM,
        ...     declared_license="Apache-2.0",
        ...     download_url="https://github.com/zephyrproject-rtos/zephyr"
        ... )
        >>> pkg.external_references.append(ExternalReference(
        ...     ref_type="purl",
        ...     locator="pkg:github/zephyrproject-rtos/zephyr@v3.5.0"
        ... ))
        >>> pkg.has_files
        False

    Note:
        The verification_code and license_info_from_files are typically
        populated by the scanner after files have been analyzed, not
        during initial package creation.
    """

    version: str = ""
    supplier: str = ""
    download_url: str = ""
    revision: str = ""
    purpose: PackagePurpose | None = None
    declared_license: str = "NOASSERTION"
    concluded_license: str = "NOASSERTION"
    copyright_text: str = "NOASSERTION"
    external_references: list[ExternalReference] = field(default_factory=list)
    files: dict[str, SBOMFile] = field(default_factory=dict)
    relationships: list[SBOMRelationship] = field(default_factory=list)
    verification_code: str = ""
    license_info_from_files: list[str] = field(default_factory=list)
    relative_base_dir: str = ""
    primary_build_file: SBOMFile | None = None

    @property
    def element_type(self) -> ElementType:
        """Return ElementType.PACKAGE."""
        return ElementType.PACKAGE

    def add_file(self, file: SBOMFile) -> None:
        """
        Add a file to this package.

        Args:
            file: The SBOMFile to add. Its internal_id will be used
                as the key in the files dict.
        """
        self.files[file.internal_id] = file

    @property
    def has_files(self) -> bool:
        """
        Check if this package contains any files.

        Returns:
            True if the package has at least one file, False otherwise.
        """
        return len(self.files) > 0
