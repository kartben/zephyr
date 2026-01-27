# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
SBOM Document model.

This module defines the SBOMDocument class, which is the top-level container
for SBOM information. A document contains packages, which in turn contain
files. Relationships can exist between any elements within or across documents.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import TYPE_CHECKING

from zspdx.model.base import ElementType, SBOMElement

if TYPE_CHECKING:
    from zspdx.model.file import SBOMFile
    from zspdx.model.package import SBOMPackage
    from zspdx.model.relationship import SBOMRelationship


@dataclass
class DocumentMetadata:
    """
    Metadata about the SBOM document itself.

    This contains information about the document, not its contents.
    Format-specific fields (like SPDX namespace URLs or CycloneDX serial
    numbers) are derived during serialization based on these values.

    Attributes:
        name: Document name (e.g., "zephyr-sources", "build", "app-sources").
            Used for identification and as the basis for format-specific
            document identifiers.
        namespace_prefix: Base URI for document namespace generation.
            For SPDX, this becomes the DocumentNamespace field.
            Should not end with "/".
        doc_ref_id: Logical reference ID for cross-document references.
            For SPDX, this becomes the DocumentRef- prefix used when
            referencing elements in this document from other documents.
        creator_tool: Name of the tool that created this document.
            Defaults to "Zephyr SPDX builder".
        creator_organization: Organization that created this document.
            Optional; may be empty if not applicable.

    Example:
        >>> metadata = DocumentMetadata(
        ...     name="my-project",
        ...     namespace_prefix="https://example.com/sbom/my-project",
        ...     doc_ref_id="DocumentRef-my-project",
        ...     creator_tool="My SBOM Generator v1.0"
        ... )
    """

    name: str = ""
    namespace_prefix: str = ""
    doc_ref_id: str = ""
    creator_tool: str = "Zephyr SPDX builder"
    creator_organization: str = ""


@dataclass
class SBOMDocument(SBOMElement):
    """
    Represents an SBOM document containing packages and files.

    A document is the top-level container for SBOM information. It contains
    packages, which in turn contain files. Relationships can exist between
    any elements within or across documents.

    Documents track their external dependencies (references to other documents)
    and any custom license identifiers that need to be declared.

    Attributes:
        metadata: Document-level metadata including name and namespace.
        packages: Dict mapping internal package IDs to SBOMPackage objects.
            Keys are the package's internal_id for efficient lookup.
        relationships: List of relationships owned by this document.
            These are typically DESCRIBES relationships linking the document
            to its primary packages.
        external_documents: Set of other SBOMDocument objects that are
            referenced by relationships in this document. Used to generate
            external document reference declarations in serialized output.
        custom_license_ids: Set of non-standard license identifiers found
            in files within this document. These need special handling in
            most SBOM formats (e.g., LicenseRef- declarations in SPDX).
        document_hash: Hash of the serialized document. This is populated
            AFTER the document has been written to disk, as it requires
            the complete serialized output. Used by other documents that
            reference this one.

    Example:
        >>> from zspdx.model import SBOMDocument, SBOMPackage, DocumentMetadata
        >>> doc = SBOMDocument(
        ...     internal_id="build-doc",
        ...     name="build"
        ... )
        >>> doc.metadata = DocumentMetadata(
        ...     name="build",
        ...     namespace_prefix="https://example.com/sbom/build"
        ... )
        >>> pkg = SBOMPackage(internal_id="kernel", name="zephyr_kernel")
        >>> doc.add_package(pkg)
        >>> len(doc.packages)
        1

    Note:
        The document_hash is only populated after serialization. If you need
        to reference this document from another document, ensure it has been
        serialized first so the hash is available.
    """

    metadata: DocumentMetadata = field(default_factory=DocumentMetadata)
    packages: dict[str, SBOMPackage] = field(default_factory=dict)
    relationships: list[SBOMRelationship] = field(default_factory=list)
    external_documents: set[SBOMDocument] = field(default_factory=set)
    custom_license_ids: set[str] = field(default_factory=set)
    document_hash: str = ""

    # Internal tracking for unique ID generation during walking
    _element_counts: dict[str, int] = field(default_factory=dict)
    # Maps absolute file paths to SBOMFile objects for quick lookup
    _file_links: dict[str, SBOMFile] = field(default_factory=dict)

    @property
    def element_type(self) -> ElementType:
        """Return ElementType.DOCUMENT."""
        return ElementType.DOCUMENT

    def add_package(self, package: SBOMPackage) -> None:
        """
        Add a package to this document.

        Args:
            package: The SBOMPackage to add. Its internal_id will be used
                as the key in the packages dict.
        """
        self.packages[package.internal_id] = package

    def get_file_by_path(self, abspath: str) -> SBOMFile | None:
        """
        Find a file by its absolute filesystem path.

        Args:
            abspath: The absolute path to search for.

        Returns:
            The SBOMFile object if found, None otherwise.
        """
        return self._file_links.get(abspath)

    def register_file(self, file: SBOMFile, abspath: str) -> None:
        """
        Register a file for path-based lookup.

        This should be called when adding files to packages within this
        document, to enable efficient path-based lookups later.

        Args:
            file: The SBOMFile to register.
            abspath: The absolute filesystem path of the file.
        """
        self._file_links[abspath] = file

    def get_element_count(self, key: str) -> int:
        """
        Get the current count for an element key (for unique ID generation).

        This is used internally during document construction to generate
        unique internal IDs when multiple elements share the same base name.

        Args:
            key: The key to look up (typically a filename or package name).

        Returns:
            The current count for this key, starting from 0.
        """
        return self._element_counts.get(key, 0)

    def increment_element_count(self, key: str) -> int:
        """
        Increment and return the count for an element key.

        Args:
            key: The key to increment.

        Returns:
            The new count value after incrementing.
        """
        count = self._element_counts.get(key, 0) + 1
        self._element_counts[key] = count
        return count
