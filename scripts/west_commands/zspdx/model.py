# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Dict, List, Optional, Set, Union


class ComponentPurpose(Enum):
    """Format-agnostic component purpose types."""
    APPLICATION = "APPLICATION"
    FRAMEWORK = "FRAMEWORK"
    LIBRARY = "LIBRARY"
    CONTAINER = "CONTAINER"
    OPERATING_SYSTEM = "OPERATING_SYSTEM"
    DEVICE = "DEVICE"
    FIRMWARE = "FIRMWARE"
    SOURCE = "SOURCE"
    ARCHIVE = "ARCHIVE"
    FILE = "FILE"
    INSTALL = "INSTALL"
    PLATFORM = "PLATFORM"
    OTHER = "OTHER"
    UNKNOWN = "UNKNOWN"


@dataclass
class SBOMExternalReference:
    """Format-agnostic external reference for components."""
    # Reference type (e.g., "purl", "cpe23", "vcs", "website")
    reference_type: str = ""

    # Locator or identifier value (URL, URN, CPE, PURL, etc.)
    locator: str = ""

    # Optional human-readable note
    comment: str = ""

    # Flexible metadata storage for format-specific needs
    metadata: Dict[str, Any] = field(default_factory=dict)


PurposeType = Union[ComponentPurpose, str]
ExternalReference = Union[SBOMExternalReference, str]
ElementRef = Union["SBOMComponent", "SBOMFile", str]


@dataclass
class SBOMFile:
    """Format-agnostic representation of a file in the SBOM."""
    # Optional stable identifier (serializer-specific or user-provided)
    element_id: str = ""

    # Absolute path to the file on disk
    path: str = ""

    # Relative path (calculated from component's base_dir)
    relative_path: str = ""

    # File hashes: {"SHA1": "...", "SHA256": "...", "MD5": "..."}
    hashes: Dict[str, str] = field(default_factory=dict)

    # Concluded license expression
    concluded_license: str = "NOASSERTION"

    # List of license identifiers found in the file
    license_info_in_file: List[str] = field(default_factory=list)

    # Copyright text
    copyright_text: str = "NOASSERTION"

    # Relationships where this file is the source (from_element)
    relationships: List['SBOMRelationship'] = field(default_factory=list)

    # Component that owns this file (set during processing)
    component: Optional['SBOMComponent'] = None

    # Flexible metadata storage for format-specific needs
    metadata: Dict[str, Any] = field(default_factory=dict)


@dataclass
class SBOMRelationship:
    """Format-agnostic representation of a relationship between SBOM elements."""
    # Optional stable identifier (serializer-specific or user-provided)
    element_id: str = ""

    # Source element (can be SBOMComponent, SBOMFile, or string identifier)
    from_element: Optional[ElementRef] = None

    # Target element(s) (can be SBOMComponent, SBOMFile, or string identifier)
    to_elements: List[ElementRef] = field(default_factory=list)

    # Relationship type (e.g., "GENERATED_FROM", "DEPENDS_ON", "CONTAINS")
    relationship_type: str = ""

    # Flexible metadata storage for format-specific needs
    metadata: Dict[str, Any] = field(default_factory=dict)


@dataclass
class SBOMComponent:
    """Format-agnostic representation of a component (package) in the SBOM."""
    # Optional stable identifier (serializer-specific or user-provided)
    element_id: str = ""

    # Component name
    name: str = ""

    # Component version
    version: str = ""

    # Component revision (git commit, etc.)
    revision: str = ""

    # Component URL/repository
    url: str = ""

    # Component purpose
    purpose: PurposeType = ComponentPurpose.LIBRARY

    # Base directory for calculating relative paths of files
    base_dir: str = ""

    # Files contained in this component
    # Key: file path (absolute) or identifier, Value: SBOMFile
    files: Dict[str, SBOMFile] = field(default_factory=dict)

    # Relationships where this component is the source (from_element)
    relationships: List[SBOMRelationship] = field(default_factory=list)

    # Verification code (calculated from file hashes)
    verification_code: str = ""

    # Concluded license expression for the component
    concluded_license: str = "NOASSERTION"

    # Declared license expression
    declared_license: str = "NOASSERTION"

    # List of licenses found in component's files
    license_info_from_files: List[str] = field(default_factory=list)

    # Copyright text
    copyright_text: str = "NOASSERTION"

    # External references (typed references or raw locator strings)
    external_references: List[ExternalReference] = field(default_factory=list)

    # Supplier/vendor information
    supplier: str = ""

    # If this component represents a build target, the main build artifact file
    target_build_file: Optional[SBOMFile] = None

    # Flexible metadata storage for format-specific needs
    metadata: Dict[str, Any] = field(default_factory=dict)


@dataclass
class SBOMData:
    """Format-agnostic container for all SBOM data."""
    # Components (packages) in the SBOM
    # Key: component name, Value: SBOMComponent
    components: Dict[str, SBOMComponent] = field(default_factory=dict)

    # All files in the SBOM (indexed by absolute path)
    # Key: absolute file path, Value: SBOMFile
    files: Dict[str, SBOMFile] = field(default_factory=dict)

    # Top-level relationships (not owned by components or files)
    relationships: List[SBOMRelationship] = field(default_factory=list)

    # Custom license IDs that need to be declared
    custom_license_ids: Set[str] = field(default_factory=set)

    # Optional base namespace for generating stable IDs (serializer-specific)
    id_namespace: str = ""

    # Build directory path
    build_dir: str = ""

    # Flexible metadata storage for format-specific needs
    metadata: Dict[str, Any] = field(default_factory=dict)

    def add_component(self, component: SBOMComponent) -> None:
        """Add a component to the SBOM."""
        self.components[component.name] = component

    def add_file(self, file: SBOMFile) -> None:
        """Add a file to the SBOM."""
        self.files[file.path] = file

    def add_relationship(self, relationship: SBOMRelationship) -> None:
        """Add a relationship to the SBOM."""
        self.relationships.append(relationship)

    def get_component(self, name: str) -> Optional[SBOMComponent]:
        """Get a component by name or identifier."""
        component = self.components.get(name)
        if component:
            return component
        for comp in self.components.values():
            if comp.name == name or comp.element_id == name:
                return comp
        return None

    def get_file(self, path: str) -> Optional[SBOMFile]:
        """Get a file by absolute path or identifier."""
        file_obj = self.files.get(path)
        if file_obj:
            return file_obj
        for f in self.files.values():
            if f.path == path or f.element_id == path:
                return f
        return None
