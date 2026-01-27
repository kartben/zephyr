# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

from dataclasses import dataclass, field
from enum import Enum
from typing import Dict, List, Optional, Set


class ComponentPurpose(Enum):
    """Format-agnostic component purpose types.

    Defines the primary purpose or role of a component within the software
    bill of materials. These purpose types are independent of any specific
    SBOM format (SPDX 2.x, SPDX 3.0, CycloneDX, etc.).

    Attributes:
        APPLICATION: Represents an executable application or final product
        LIBRARY: Represents a reusable library or module
        SOURCE: Represents source code or source-level component
        FILE: Represents an individual file component
    """

    APPLICATION = "APPLICATION"
    LIBRARY = "LIBRARY"
    SOURCE = "SOURCE"
    FILE = "FILE"
    # Add more as needed


@dataclass
class SBOMFile:
    """Format-agnostic representation of a file in the SBOM.

    Represents a single file within a software component, including its
    identifying information, license data, and cryptographic checksums.
    This model is independent of specific SBOM formats and serves as an
    intermediate representation for serialization.

    Attributes:
        path: Absolute path to the file on the local filesystem.
        relative_path: Path relative to the owning component's base_dir.
            Calculated during processing based on the component context.
        hashes: Dictionary of cryptographic hash values for the file.
            Keys are hash algorithm names (e.g., "SHA1", "SHA256", "MD5"),
            values are hex-encoded hash strings.
        concluded_license: The concluded license expression for this file.
            Defaults to "NOASSERTION" if license cannot be determined.
            Should use SPDX license expression syntax.
        license_info_in_file: List of license identifiers detected within
            the file content (e.g., from SPDX-License-Identifier tags).
        copyright_text: Copyright notice text found in the file.
            Defaults to "NOASSERTION" if no copyright information is found.
        relationships: List of relationships where this file is the source
            element (from_element). Examples include GENERATED_FROM or
            DEPENDENCY_OF relationships.
        component: Reference to the parent SBOMComponent that owns this file.
            Set during SBOM processing and assembly.
        metadata: Flexible dictionary for storing format-specific or
            tool-specific metadata that doesn't fit standard attributes.
    """

    path: str = ""
    relative_path: str = ""
    hashes: Dict[str, str] = field(default_factory=dict)
    concluded_license: str = "NOASSERTION"
    license_info_in_file: List[str] = field(default_factory=list)
    copyright_text: str = "NOASSERTION"
    relationships: List['SBOMRelationship'] = field(default_factory=list)
    component: Optional['SBOMComponent'] = None
    metadata: Dict[str, any] = field(default_factory=dict)


@dataclass
class SBOMRelationship:
    """Format-agnostic representation of a relationship between SBOM elements.

    Captures semantic relationships between components, files, and other SBOM
    elements. Relationships describe how elements are connected, such as
    dependencies, containment, generation, or derivation.

    Attributes:
        from_element: The source element of the relationship. Can be an
            SBOMComponent instance, SBOMFile instance, or a string identifier
            (for referencing external or yet-to-be-resolved elements).
        to_elements: List of target elements in the relationship. Each element
            can be an SBOMComponent, SBOMFile, or string identifier.
            Note: SPDX 2.x typically uses single-element relationships, while
            SPDX 3.0 and other formats may support multiple targets.
        relationship_type: String describing the semantic type of relationship.
            Common examples include:
            - "GENERATED_FROM": target was used to generate source
            - "HAS_PREREQUISITE": source requires target as a prerequisite
            - "STATIC_LINK": source statically links to target
            - "CONTAINS": source contains target as a sub-element
            - "DEPENDS_ON": source depends on target
        metadata: Flexible dictionary for storing format-specific relationship
            properties or additional context not covered by standard attributes.
    """

    from_element: any = None
    to_elements: List[any] = field(default_factory=list)
    relationship_type: str = ""
    metadata: Dict[str, any] = field(default_factory=dict)


@dataclass
class SBOMComponent:
    """Format-agnostic representation of a component (package) in the SBOM.

    Represents a logical software component, such as a library, application,
    or module. Components typically contain multiple files and can have
    relationships with other components (dependencies, containment, etc.).
    This model is independent of specific SBOM formats.

    Attributes:
        name: Human-readable name identifying the component (e.g., "zephyr",
            "newlib", "mbedtls").
        version: Semantic version or version identifier for the component
            (e.g., "3.7.0", "1.2.3-rc1").
        revision: Source control revision identifier, typically a Git commit
            hash (e.g., "a1b2c3d4e5f6...").
        url: URL to the component's homepage, repository, or download location.
        purpose: Enum value indicating the component's role (APPLICATION,
            LIBRARY, SOURCE, or FILE). Defaults to LIBRARY.
        base_dir: Base filesystem directory for this component, used as the
            reference point when calculating relative paths for contained files.
        files: Dictionary of files contained within this component.
            Key: absolute file path or unique identifier
            Value: SBOMFile instance
        relationships: List of relationships where this component is the source
            (from_element). Examples include dependency relationships, build
            relationships, or containment relationships.
        verification_code: Cryptographic verification code calculated from
            the hashes of all files in the component. Used to verify component
            integrity without storing individual file hashes.
        concluded_license: The concluded license expression for the entire
            component. Should use SPDX license expression syntax. Defaults to
            "NOASSERTION" if not determined.
        declared_license: License expression as declared by the component's
            authors or metadata files. May differ from concluded_license.
        license_info_from_files: Aggregated list of all license identifiers
            found in the component's files.
        copyright_text: Copyright notice text for the component. Defaults to
            "NOASSERTION" if not available.
        external_references: List of external identifier strings such as:
            - CPE (Common Platform Enumeration) identifiers
            - PURL (Package URL) identifiers
            - Other ecosystem-specific identifiers
        supplier: Name of the organization or entity that supplied/created
            the component (e.g., "Organization: The Zephyr Project").
        target_build_file: If this component represents a build target (e.g.,
            a compiled application or library), this points to the primary
            output artifact file (e.g., the .elf or .a file).
        metadata: Flexible dictionary for storing format-specific or
            tool-specific metadata beyond standard attributes.
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
    metadata: Dict[str, any] = field(default_factory=dict)


@dataclass
class SBOMData:
    """Format-agnostic container for all SBOM data.

    Serves as the root container for a complete Software Bill of Materials,
    aggregating all components, files, relationships, and metadata. This
    intermediate representation is independent of specific SBOM output formats
    (SPDX 2.x, SPDX 3.0, CycloneDX, etc.) and is used by format-specific
    serializers to generate the final SBOM documents.

    Attributes:
        components: Dictionary of all software components in the SBOM.
            Key: component name or unique identifier
            Value: SBOMComponent instance
        files: Dictionary of all files tracked in the SBOM, indexed by their
            absolute filesystem paths. This provides quick lookup of files
            regardless of which component owns them.
            Key: absolute file path
            Value: SBOMFile instance
        relationships: List of top-level relationships that are not owned by
            any specific component or file. These typically include high-level
            relationships like document-level DESCRIBES relationships or
            cross-component dependencies.
        custom_license_ids: Set of non-standard license identifiers that appear
            in the SBOM and need to be declared with full license text in the
            output document (e.g., custom proprietary licenses or licenses not
            in the SPDX license list).
        namespace_prefix: Base namespace/URI prefix used for generating unique
            identifiers in format-specific serializers (e.g.,
            "https://example.com/spdx/").
        build_dir: Absolute path to the build directory. Used as a reference
            point for resolving file paths and generating relative paths.
        metadata: Flexible dictionary for storing SBOM-level metadata such as
            creation timestamps, tool information, or format-specific settings.
        filename_counts: Dictionary tracking the occurrence count of each
            filename (basename) across all files in the SBOM. Used by
            format-specific serializers to generate unique identifiers when
            multiple files share the same name.
            Key: filename (basename, e.g., "main.c")
            Value: occurrence count

    Methods:
        add_component: Add a component to the SBOM's component registry.
        add_file: Add a file to the SBOM's global file registry.
        add_relationship: Add a top-level relationship to the SBOM.
        get_component: Retrieve a component by its name.
        get_file: Retrieve a file by its absolute path.
    """

    components: Dict[str, SBOMComponent] = field(default_factory=dict)
    files: Dict[str, SBOMFile] = field(default_factory=dict)
    relationships: List[SBOMRelationship] = field(default_factory=list)
    custom_license_ids: Set[str] = field(default_factory=set)
    namespace_prefix: str = ""
    build_dir: str = ""
    metadata: Dict[str, any] = field(default_factory=dict)
    filename_counts: Dict[str, int] = field(default_factory=dict)

    def add_component(self, component: SBOMComponent) -> None:
        """Add a component to the SBOM.

        Args:
            component: The SBOMComponent instance to add. Will be indexed
                by its name attribute.
        """
        self.components[component.name] = component

    def add_file(self, file: SBOMFile) -> None:
        """Add a file to the SBOM's global file registry.

        Args:
            file: The SBOMFile instance to add. Will be indexed by its
                path attribute (absolute path).
        """
        self.files[file.path] = file

    def add_relationship(self, relationship: SBOMRelationship) -> None:
        """Add a top-level relationship to the SBOM.

        Top-level relationships are those not owned by a specific component
        or file, typically document-level or cross-cutting relationships.

        Args:
            relationship: The SBOMRelationship instance to add.
        """
        self.relationships.append(relationship)

    def get_component(self, name: str) -> Optional[SBOMComponent]:
        """Retrieve a component by its name.

        Args:
            name: The name of the component to retrieve.

        Returns:
            The SBOMComponent instance if found, None otherwise.
        """
        return self.components.get(name)

    def get_file(self, path: str) -> Optional[SBOMFile]:
        """Retrieve a file by its absolute path.

        Args:
            path: The absolute filesystem path of the file to retrieve.

        Returns:
            The SBOMFile instance if found, None otherwise.
        """
        return self.files.get(path)
