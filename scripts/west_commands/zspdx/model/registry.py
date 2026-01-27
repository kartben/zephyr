# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
Element registry for managing cross-references.

This module provides the ElementRegistry class, which maintains indexes
for efficient element lookup across multiple documents during SBOM generation.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from zspdx.model.document import SBOMDocument
    from zspdx.model.file import SBOMFile
    from zspdx.model.package import SBOMPackage


class ElementRegistry:
    """
    Registry for tracking all SBOM elements across documents.

    This provides a central location for resolving element references
    by various identifiers (file paths, package names, internal IDs).
    The registry is particularly useful during relationship resolution,
    when we need to find elements that may be in different documents.

    The registry maintains several indexes for efficient lookup:
    - Documents by internal ID
    - Files by absolute path (mapped to their containing document)
    - Packages by name

    Example:
        >>> from zspdx.model import ElementRegistry, SBOMDocument, SBOMPackage
        >>> registry = ElementRegistry()
        >>> doc = SBOMDocument(internal_id="build", name="build")
        >>> registry.register_document(doc)
        >>> pkg = SBOMPackage(internal_id="kernel", name="zephyr_kernel")
        >>> doc.add_package(pkg)
        >>> registry.register_package(pkg)
        >>> found = registry.find_package_by_name("zephyr_kernel")
        >>> found.internal_id
        'kernel'

    Note:
        A single registry instance should be used for an entire SBOM
        generation session to enable cross-document element resolution.
    """

    def __init__(self) -> None:
        """Initialize an empty registry."""
        # All documents in this SBOM generation, keyed by internal_id
        self.documents: dict[str, SBOMDocument] = {}
        # Maps absolute file paths to the document containing that file
        self.file_path_index: dict[str, SBOMDocument] = {}
        # Maps package names to package objects
        self.package_name_index: dict[str, SBOMPackage] = {}

    def register_document(self, doc: SBOMDocument) -> None:
        """
        Register a document in the registry.

        Args:
            doc: The SBOMDocument to register. Its internal_id will be
                used as the key.
        """
        self.documents[doc.internal_id] = doc

    def register_file(self, file: SBOMFile, doc: SBOMDocument) -> None:
        """
        Register a file's path for lookup.

        This creates an index entry mapping the file's absolute path to
        its containing document, and also registers the file with the
        document itself.

        Args:
            file: The SBOMFile to register.
            doc: The SBOMDocument containing this file.
        """
        self.file_path_index[file.absolute_path] = doc
        doc.register_file(file, file.absolute_path)

    def register_package(self, package: SBOMPackage) -> None:
        """
        Register a package by name.

        Args:
            package: The SBOMPackage to register. Its name will be used
                as the key.
        """
        self.package_name_index[package.name] = package

    def find_document_for_file(self, abspath: str) -> SBOMDocument | None:
        """
        Find the document containing a file by its absolute path.

        Args:
            abspath: The absolute filesystem path to search for.

        Returns:
            The SBOMDocument containing the file, or None if not found.
        """
        return self.file_path_index.get(abspath)

    def find_file_by_path(self, abspath: str) -> tuple[SBOMFile, SBOMDocument] | None:
        """
        Find a file and its document by absolute path.

        Args:
            abspath: The absolute filesystem path to search for.

        Returns:
            A tuple of (SBOMFile, SBOMDocument) if found, None otherwise.
        """
        doc = self.file_path_index.get(abspath)
        if doc:
            file = doc.get_file_by_path(abspath)
            if file:
                return (file, doc)
        return None

    def find_package_by_name(self, name: str) -> SBOMPackage | None:
        """
        Find a package by name.

        Args:
            name: The package name to search for.

        Returns:
            The SBOMPackage if found, None otherwise.
        """
        return self.package_name_index.get(name)

    def find_document_by_id(self, internal_id: str) -> SBOMDocument | None:
        """
        Find a document by its internal ID.

        Args:
            internal_id: The document's internal_id to search for.

        Returns:
            The SBOMDocument if found, None otherwise.
        """
        return self.documents.get(internal_id)
