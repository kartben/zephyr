# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

from dataclasses import dataclass, field
from enum import Enum
from typing import Dict, List, Optional, Set


class ComponentPurpose(Enum):
    """Format-agnostic component purpose types."""
    APPLICATION = "APPLICATION"
    LIBRARY = "LIBRARY"
    SOURCE = "SOURCE"
    FILE = "FILE"
    # Add more as needed


@dataclass
class SBOMFile:
    """Format-agnostic representation of a file in the SBOM."""
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
    metadata: Dict[str, any] = field(default_factory=dict)


@dataclass
class SBOMRelationship:
    """Format-agnostic representation of a relationship between SBOM elements."""
    # Source element (can be SBOMComponent, SBOMFile, or string identifier)
    from_element: any = None

    # Target element(s) (can be SBOMComponent, SBOMFile, or string identifier)
    # For SPDX 2.x compatibility, this is typically a single element
    # For SPDX 3.0, this can be a list
    to_elements: List[any] = field(default_factory=list)

    # Relationship type (e.g., "GENERATED_FROM", "HAS_PREREQUISITE", "STATIC_LINK", "CONTAINS")
    relationship_type: str = ""

    # Flexible metadata storage for format-specific needs
    metadata: Dict[str, any] = field(default_factory=dict)


@dataclass
class SBOMComponent:
    """Format-agnostic representation of a component (package) in the SBOM."""
    # Component name
    name: str = ""

    # Component version
    version: str = ""

    # Component revision (git commit, etc.)
    revision: str = ""

    # Component URL/repository
    url: str = ""

    # Component purpose
    purpose: ComponentPurpose = ComponentPurpose.LIBRARY

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

    # External references (CPE, PURL, etc.)
    external_references: List[str] = field(default_factory=list)

    # Supplier/vendor information
    supplier: str = ""

    # If this component represents a build target, the main build artifact file
    target_build_file: Optional[SBOMFile] = None

    # Flexible metadata storage for format-specific needs
    metadata: Dict[str, any] = field(default_factory=dict)


@dataclass
class SBOMData:
    """Format-agnostic container for all SBOM data."""
    # Components (packages) in the SBOM
    # Key: component name/identifier, Value: SBOMComponent
    components: Dict[str, SBOMComponent] = field(default_factory=dict)

    # All files in the SBOM (indexed by absolute path)
    # Key: absolute file path, Value: SBOMFile
    files: Dict[str, SBOMFile] = field(default_factory=dict)

    # Top-level relationships (not owned by components or files)
    relationships: List[SBOMRelationship] = field(default_factory=list)

    # Custom license IDs that need to be declared
    custom_license_ids: Set[str] = field(default_factory=set)

    # Namespace prefix for generating IDs (format-specific serializers will use this)
    namespace_prefix: str = ""

    # Build directory path
    build_dir: str = ""

    # Flexible metadata storage for format-specific needs
    metadata: Dict[str, any] = field(default_factory=dict)

    # Track filename occurrences for generating unique IDs (format-specific)
    # Key: filename (basename), Value: count
    filename_counts: Dict[str, int] = field(default_factory=dict)

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
        """Get a component by name."""
        return self.components.get(name)

    def get_file(self, path: str) -> Optional[SBOMFile]:
        """Get a file by absolute path."""
        return self.files.get(path)
