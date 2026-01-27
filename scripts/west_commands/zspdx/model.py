# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
Format-agnostic SBOM data model.

This module provides a serialization-independent representation of Software
Bill of Materials (SBOM) data. The model is designed to be used as an
intermediate representation that can be serialized to various SBOM formats
including SPDX 2.x, SPDX 3.0, CycloneDX, and others.

The model consists of four main classes:
- SBOMData: Top-level container for all SBOM information
- SBOMComponent: Represents a software component/package
- SBOMFile: Represents a file within a component
- SBOMRelationship: Represents relationships between elements

Each class includes a `metadata` field for storing format-specific data
that doesn't fit the common model but may be needed by specific serializers.
"""

from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Dict, List, Optional, Set, Union


class ComponentPurpose(Enum):
    """
    Purpose classification for software components.

    These values map to common SBOM format classifications:
    - SPDX 2.x/3.0: PrimaryPackagePurpose / software_SoftwarePurpose
    - CycloneDX: component type (application, library, etc.)
    """
    APPLICATION = "APPLICATION"
    LIBRARY = "LIBRARY"
    SOURCE = "SOURCE"
    FILE = "FILE"
    FRAMEWORK = "FRAMEWORK"
    CONTAINER = "CONTAINER"
    FIRMWARE = "FIRMWARE"
    DEVICE = "DEVICE"
    OPERATING_SYSTEM = "OPERATING_SYSTEM"
    DATA = "DATA"
    OTHER = "OTHER"


@dataclass
class SBOMFile:
    """
    Format-agnostic representation of a file in the SBOM.

    Represents a single file that is part of a software component. Files can
    have hashes for integrity verification, license information, and
    relationships to other SBOM elements.

    Attributes:
        path: Absolute path to the file on disk.
        relative_path: Path relative to the owning component's base_dir.
        hashes: File checksums keyed by algorithm (e.g., "SHA1", "SHA256", "MD5").
        concluded_license: SPDX license expression representing the concluded license.
        license_info_in_file: List of license identifiers found in the file.
        copyright_text: Copyright statement(s) for the file.
        relationships: Relationships where this file is the source element.
        component: Reference to the component that contains this file.
        metadata: Extensible storage for format-specific attributes.
    """
    path: str = ""
    relative_path: str = ""
    hashes: Dict[str, str] = field(default_factory=dict)
    concluded_license: str = "NOASSERTION"
    license_info_in_file: List[str] = field(default_factory=list)
    copyright_text: str = "NOASSERTION"
    relationships: List['SBOMRelationship'] = field(default_factory=list)
    component: Optional['SBOMComponent'] = None
    metadata: Dict[str, Any] = field(default_factory=dict)


@dataclass
class SBOMRelationship:
    """
    Format-agnostic representation of a relationship between SBOM elements.

    Relationships express how SBOM elements (components, files) relate to
    each other. Common relationship types include dependency, containment,
    and generation relationships.

    The relationship_type uses format-neutral strings that serializers map
    to format-specific values:
    - "CONTAINS": Component/file contains another element
    - "DEPENDS_ON": Element depends on another element
    - "GENERATED_FROM": Element was generated from source element(s)
    - "HAS_PREREQUISITE": Element requires another as prerequisite
    - "STATIC_LINK": Element statically links to another
    - "DYNAMIC_LINK": Element dynamically links to another
    - "BUILD_TOOL": Element was built using a tool
    - "DEV_DEPENDENCY": Development-time dependency
    - "OPTIONAL_DEPENDENCY": Optional runtime dependency
    - "DESCRIBES": Document/element describes another element
    - "OTHER": Other relationship type (use metadata for details)

    Attributes:
        from_element: Source element (SBOMComponent, SBOMFile, or identifier string).
        to_elements: Target element(s). Supports single or multiple targets.
        relationship_type: Type of relationship (see above for common values).
        metadata: Extensible storage for format-specific attributes.
    """
    from_element: Union['SBOMComponent', 'SBOMFile', str, None] = None
    to_elements: List[Union['SBOMComponent', 'SBOMFile', str]] = field(default_factory=list)
    relationship_type: str = ""
    metadata: Dict[str, Any] = field(default_factory=dict)


@dataclass
class SBOMComponent:
    """
    Format-agnostic representation of a software component/package.

    A component represents a logical unit of software such as a library,
    application, or module. Components contain files and can have
    relationships to other components.

    Attributes:
        name: Component identifier/name.
        version: Version string (e.g., "1.2.3").
        revision: Source control revision (e.g., git commit SHA).
        url: Component homepage or repository URL.
        purpose: Classification of the component's purpose.
        base_dir: Root directory for calculating file relative paths.
        files: Files belonging to this component, keyed by absolute path.
        relationships: Relationships where this component is the source.
        verification_code: Package verification code (calculated from file hashes).
        concluded_license: SPDX license expression for concluded license.
        declared_license: SPDX license expression for declared license.
        license_info_from_files: Aggregated licenses from contained files.
        copyright_text: Copyright statement(s) for the component.
        external_references: External identifiers (CPE, PURL, etc.).
        supplier: Supplier/vendor name or organization.
        target_build_file: If a build target, the primary build artifact.
        metadata: Extensible storage for format-specific attributes.
    """
    name: str = ""
    version: str = ""
    revision: str = ""
    url: str = ""
    purpose: ComponentPurpose = ComponentPurpose.LIBRARY
    base_dir: str = ""
    files: Dict[str, SBOMFile] = field(default_factory=dict)
    relationships: List[SBOMRelationship] = field(default_factory=list)
    verification_code: str = ""
    concluded_license: str = "NOASSERTION"
    declared_license: str = "NOASSERTION"
    license_info_from_files: List[str] = field(default_factory=list)
    copyright_text: str = "NOASSERTION"
    external_references: List[str] = field(default_factory=list)
    supplier: str = ""
    target_build_file: Optional[SBOMFile] = None
    metadata: Dict[str, Any] = field(default_factory=dict)


@dataclass
class SBOMData:
    """
    Format-agnostic container for all SBOM data.

    This is the top-level container that holds all components, files, and
    relationships for a software bill of materials. Serializers consume
    this data structure to produce format-specific output (SPDX, CycloneDX, etc.).

    Attributes:
        components: All components in the SBOM, keyed by name.
        files: All files in the SBOM, keyed by absolute path.
        relationships: Top-level relationships not owned by specific elements.
        custom_license_ids: Non-standard license IDs requiring declaration.
        namespace_prefix: Base URI/namespace for generating element identifiers.
        build_dir: Path to the build directory.
        metadata: Extensible storage for format-specific or document-level data.
            Common keys include:
            - "build_info": Dict with build tool information (compiler, cmake, etc.)
            - "document_name": Custom document name
            - "creator_tool": Tool that created the SBOM
        filename_counts: Tracks filename occurrences for unique ID generation.
    """
    components: Dict[str, SBOMComponent] = field(default_factory=dict)
    files: Dict[str, SBOMFile] = field(default_factory=dict)
    relationships: List[SBOMRelationship] = field(default_factory=list)
    custom_license_ids: Set[str] = field(default_factory=set)
    namespace_prefix: str = ""
    build_dir: str = ""
    metadata: Dict[str, Any] = field(default_factory=dict)
    filename_counts: Dict[str, int] = field(default_factory=dict)

    def add_component(self, component: SBOMComponent) -> None:
        """Add a component to the SBOM."""
        self.components[component.name] = component

    def add_file(self, file: SBOMFile) -> None:
        """Add a file to the SBOM (top-level registry)."""
        self.files[file.path] = file

    def add_relationship(self, relationship: SBOMRelationship) -> None:
        """Add a top-level relationship to the SBOM."""
        self.relationships.append(relationship)

    def get_component(self, name: str) -> Optional[SBOMComponent]:
        """Get a component by name, or None if not found."""
        return self.components.get(name)

    def get_file(self, path: str) -> Optional[SBOMFile]:
        """Get a file by absolute path, or None if not found."""
        return self.files.get(path)
