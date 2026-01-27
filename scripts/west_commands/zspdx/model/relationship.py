# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
SBOM Relationship model.

This module defines relationship classes for representing connections between
SBOM elements. Relationships describe how packages, files, and documents are
related to each other.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto
from typing import TYPE_CHECKING

from zspdx.model.base import ElementType, SBOMElement

if TYPE_CHECKING:
    from zspdx.model.document import SBOMDocument


class RelationshipType(Enum):
    """
    Type of relationship between SBOM elements.

    These are logical relationship types that map to format-specific
    relationship strings during serialization. The naming follows SPDX
    conventions but the concepts are applicable to other formats.

    Document Relationships:
        DESCRIBES: Document describes a package (primary content).

    Derivation Relationships:
        GENERATED_FROM: Target was used to generate source (e.g., binary from source).
        DERIVED_FROM: Source is derived from target (more general than GENERATED_FROM).
        ANCESTOR_OF: Source is an ancestor of target.
        DESCENDANT_OF: Source is a descendant of target.

    Dependency Relationships:
        DEPENDS_ON: Source depends on target.
        DEPENDENCY_OF: Source is a dependency of target.
        HAS_PREREQUISITE: Source has target as a prerequisite.
        PREREQUISITE_OF: Source is a prerequisite of target.

    Containment Relationships:
        CONTAINS: Source contains target.
        CONTAINED_BY: Source is contained by target.

    Build Relationships:
        BUILD_TOOL_OF: Source is a build tool for target.
        STATIC_LINK: Source statically links to target.
        DYNAMIC_LINK: Source dynamically links to target.

    Other:
        OTHER: Any other relationship type not covered above.
    """

    # Document relationships
    DESCRIBES = auto()

    # Derivation relationships
    GENERATED_FROM = auto()
    DERIVED_FROM = auto()
    ANCESTOR_OF = auto()
    DESCENDANT_OF = auto()

    # Dependency relationships
    DEPENDS_ON = auto()
    DEPENDENCY_OF = auto()
    HAS_PREREQUISITE = auto()
    PREREQUISITE_OF = auto()

    # Containment relationships
    CONTAINS = auto()
    CONTAINED_BY = auto()

    # Build relationships
    BUILD_TOOL_OF = auto()
    STATIC_LINK = auto()
    DYNAMIC_LINK = auto()

    # Other
    OTHER = auto()


@dataclass
class SBOMRelationship(SBOMElement):
    """
    Represents a relationship between two SBOM elements.

    Relationships connect elements to describe how they are related.
    The source element "owns" the relationship and is the left side
    in relationship expressions (e.g., "A DEPENDS_ON B").

    Relationships can cross document boundaries. When the target element
    is in a different document, the external_document field is populated
    to enable proper cross-document reference generation during serialization.

    Attributes:
        source: The source element (left side) of the relationship.
            This element "owns" the relationship.
        target: The target element (right side) of the relationship.
        relationship_type: The type of relationship. See RelationshipType.
        external_document: If the target is in a different document than
            the source, this references that external document. Used to
            generate proper external document references during serialization.
            None if source and target are in the same document.

    Example:
        >>> from zspdx.model import (
        ...     SBOMRelationship, RelationshipType, SBOMFile, SBOMDocument
        ... )
        >>> # Binary generated from source file
        >>> build_file = SBOMFile(internal_id="zephyr.elf", name="zephyr.elf")
        >>> source_file = SBOMFile(internal_id="main.c", name="main.c")
        >>> rln = SBOMRelationship(
        ...     source=build_file,
        ...     target=source_file,
        ...     relationship_type=RelationshipType.GENERATED_FROM
        ... )

    Note:
        The source and target can be any SBOMElement subclass (Document,
        Package, or File). The relationship_type should be appropriate
        for the element types involved.
    """

    source: SBOMElement | None = None
    target: SBOMElement | None = None
    relationship_type: RelationshipType = RelationshipType.OTHER
    external_document: SBOMDocument | None = None

    @property
    def element_type(self) -> ElementType:
        """Return ElementType.RELATIONSHIP."""
        return ElementType.RELATIONSHIP


class PendingRelationshipElementType(Enum):
    """
    Type of element reference in a pending relationship.

    During walking, relationships may be created before all elements exist.
    This enum identifies how to resolve the element reference later.

    Attributes:
        FILE_PATH: Element is identified by its absolute file path.
        PACKAGE_NAME: Element is identified by its package/target name.
        PACKAGE_ID: Element is identified by its internal package ID.
        DOCUMENT: Element is the document itself (SPDXRef-DOCUMENT).
    """

    FILE_PATH = auto()
    PACKAGE_NAME = auto()
    PACKAGE_ID = auto()
    DOCUMENT = auto()


@dataclass
class PendingRelationship:
    """
    Temporary storage for relationships before elements are fully resolved.

    During the walking phase of SBOM generation, we may need to create
    relationships before all elements exist. This class stores the data
    needed to create the actual SBOMRelationship once all elements are
    available.

    The source and target are identified by type and identifier strings.
    These are resolved to actual SBOMElement objects after walking completes.

    Attributes:
        source_type: How to find the source element (file path, name, etc.).
        source_identifier: Value used to locate the source element.
            For FILE_PATH: absolute path to the file.
            For PACKAGE_NAME: the package/target name.
            For PACKAGE_ID: the internal package ID.
            For DOCUMENT: the document's internal_id.
        source_document: The document containing the source element.
            Used for DOCUMENT type and for resolving elements.
        target_type: How to find the target element.
        target_identifier: Value used to locate the target element.
        relationship_type: The relationship type to create.

    Example:
        >>> from zspdx.model.relationship import (
        ...     PendingRelationship,
        ...     PendingRelationshipElementType,
        ...     RelationshipType
        ... )
        >>> # Build file generated from source file
        >>> pending = PendingRelationship(
        ...     source_type=PendingRelationshipElementType.FILE_PATH,
        ...     source_identifier="/build/zephyr.elf",
        ...     target_type=PendingRelationshipElementType.FILE_PATH,
        ...     target_identifier="/src/main.c",
        ...     relationship_type=RelationshipType.GENERATED_FROM
        ... )

    Note:
        PendingRelationship objects are typically created by the walker
        and resolved by walkRelationships() after all elements exist.
    """

    source_type: PendingRelationshipElementType = PendingRelationshipElementType.FILE_PATH
    source_identifier: str = ""
    source_document: SBOMDocument | None = None
    target_type: PendingRelationshipElementType = PendingRelationshipElementType.FILE_PATH
    target_identifier: str = ""
    relationship_type: RelationshipType = RelationshipType.OTHER
