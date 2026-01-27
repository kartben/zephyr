# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
SBOM data model package.

This package provides format-agnostic data structures for representing
Software Bill of Materials (SBOM) information. The model is designed to
be independent of any specific SBOM format (SPDX, CycloneDX, etc.) and
can be serialized to various output formats through the serializers package.

Classes:
    SBOMElement: Abstract base class for all SBOM elements.
    SBOMDocument: Top-level container for SBOM data.
    SBOMPackage: Represents a software package.
    SBOMFile: Represents a file within a package.
    SBOMRelationship: Represents a relationship between elements.
    ElementRegistry: Tracks elements across documents for cross-references.

Example:
    >>> from zspdx.model import SBOMDocument, SBOMPackage, SBOMFile
    >>> doc = SBOMDocument(internal_id="my-sbom", name="My SBOM")
    >>> pkg = SBOMPackage(internal_id="app", name="myapp", version="1.0.0")
    >>> doc.add_package(pkg)
"""

from zspdx.model.base import ElementType, SBOMElement
from zspdx.model.document import DocumentMetadata, SBOMDocument
from zspdx.model.file import FileHashes, SBOMFile
from zspdx.model.package import ExternalReference, PackagePurpose, SBOMPackage
from zspdx.model.registry import ElementRegistry
from zspdx.model.relationship import (
    PendingRelationship,
    PendingRelationshipElementType,
    RelationshipType,
    SBOMRelationship,
)

__all__ = [
    # Base
    "ElementType",
    "SBOMElement",
    # Document
    "DocumentMetadata",
    "SBOMDocument",
    # Package
    "PackagePurpose",
    "ExternalReference",
    "SBOMPackage",
    # File
    "FileHashes",
    "SBOMFile",
    # Relationship
    "RelationshipType",
    "SBOMRelationship",
    "PendingRelationship",
    "PendingRelationshipElementType",
    # Registry
    "ElementRegistry",
]
