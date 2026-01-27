# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
CycloneDX-specific adapters for bom-ref generation and transformations.

This module provides the CycloneDXAdapter class, which handles the generation
of CycloneDX-compliant identifiers and mappings from the format-agnostic
SBOM model.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

from cyclonedx.model.component import ComponentType

from zspdx.model.base import ElementType
from zspdx.model.package import PackagePurpose

if TYPE_CHECKING:
    from zspdx.model.base import SBOMElement


class CycloneDXAdapter:
    """
    Adapter for CycloneDX-specific transformations.

    This adapter handles generation of CycloneDX bom-ref identifiers and
    mapping of SBOM model concepts to CycloneDX equivalents. BOM references
    are generated on-demand and cached for consistency.

    BOM-Ref Format:
        - Package: pkg-{safe-name}
        - File: file-{safe-filename}[-{count}]

    BOM-Ref Character Rules:
        - Can contain: letters, numbers, '-', '_', '.'
        - Spaces and special characters are replaced with dashes

    Attributes:
        _id_cache: Internal cache mapping internal_id to generated bom-ref.
        _name_counts: Counter for unique bom-refs per base name.

    Example:
        >>> from zspdx.adapters import CycloneDXAdapter
        >>> from zspdx.model import SBOMPackage, PackagePurpose
        >>>
        >>> adapter = CycloneDXAdapter()
        >>> pkg = SBOMPackage(internal_id="pkg1", name="my_package")
        >>> adapter.get_bom_ref(pkg)
        'pkg-my-package'
        >>>
        >>> adapter.map_purpose_to_component_type(PackagePurpose.LIBRARY)
        <ComponentType.LIBRARY: 'library'>

    Note:
        A single adapter instance should be used for an entire document
        serialization to ensure consistent bom-ref generation. Call reset()
        if you need to serialize multiple documents independently.
    """

    def __init__(self) -> None:
        """Initialize the adapter with empty caches."""
        # Cache of internal_id -> bom-ref mappings
        self._id_cache: dict[str, str] = {}
        # Counter for unique bom-refs per base name
        self._name_counts: dict[str, int] = {}

    def get_bom_ref(self, element: SBOMElement) -> str:
        """
        Get or generate the CycloneDX bom-ref for an element.

        This method returns a cached bom-ref if one was already generated
        for this element, otherwise it generates a new one, caches it,
        and returns it.

        Args:
            element: The SBOM element to get a bom-ref for.

        Returns:
            The bom-ref string.
        """
        if element.internal_id in self._id_cache:
            return self._id_cache[element.internal_id]

        bom_ref = self._generate_bom_ref(element)
        self._id_cache[element.internal_id] = bom_ref
        return bom_ref

    def _generate_bom_ref(self, element: SBOMElement) -> str:
        """
        Generate a bom-ref for an element.

        The bom-ref format depends on the element type:
        - Package: pkg-{safe-name}
        - File: file-{safe-filename}[-{count}]
        - Document: doc-{safe-name}

        Args:
            element: The SBOM element to generate a bom-ref for.

        Returns:
            The generated bom-ref string.
        """
        if element.element_type == ElementType.DOCUMENT:
            safe_name = self._convert_to_safe(element.name)
            return f"doc-{safe_name}"

        elif element.element_type == ElementType.PACKAGE:
            safe_name = self._convert_to_safe(element.name)
            return self._get_unique_ref(f"pkg-{safe_name}")

        elif element.element_type == ElementType.FILE:
            # Import here to avoid circular imports
            import os

            from zspdx.model.file import SBOMFile

            file: SBOMFile = element  # type: ignore
            # Extract just the filename from the path
            filename = os.path.basename(file.absolute_path)
            safe_name = self._convert_to_safe(filename)
            return self._get_unique_ref(f"file-{safe_name}")

        else:
            # Fallback for relationships or unknown types
            safe_id = self._convert_to_safe(element.internal_id)
            return self._get_unique_ref(f"ref-{safe_id}")

    def _convert_to_safe(self, s: str) -> str:
        """
        Convert a string to bom-ref-safe characters.

        BOM-refs can contain letters, numbers, '-', '_', and '.'.
        Other characters are replaced with '-'.

        Args:
            s: The string to convert.

        Returns:
            String containing only bom-ref-safe characters.
        """
        result = []
        for c in s:
            if c.isalnum() or c in ("-", "_", "."):
                result.append(c)
            else:
                result.append("-")
        return "".join(result)

    def _get_unique_ref(self, base_ref: str) -> str:
        """
        Generate a unique bom-ref, handling duplicates.

        When multiple elements have the same base name, a numeric suffix
        is added to ensure uniqueness.

        Args:
            base_ref: The base bom-ref without suffix.

        Returns:
            Unique bom-ref string.
        """
        count = self._name_counts.get(base_ref, 0) + 1
        self._name_counts[base_ref] = count

        if count > 1:
            return f"{base_ref}-{count}"
        return base_ref

    def map_purpose_to_component_type(
        self, purpose: PackagePurpose | None
    ) -> ComponentType:
        """
        Map PackagePurpose to CycloneDX ComponentType.

        Args:
            purpose: The PackagePurpose enum value, or None.

        Returns:
            Corresponding CycloneDX ComponentType.
        """
        if purpose is None:
            return ComponentType.LIBRARY  # Default fallback

        mapping = {
            PackagePurpose.APPLICATION: ComponentType.APPLICATION,
            PackagePurpose.LIBRARY: ComponentType.LIBRARY,
            PackagePurpose.SOURCE: ComponentType.FILE,
            PackagePurpose.FRAMEWORK: ComponentType.FRAMEWORK,
            PackagePurpose.CONTAINER: ComponentType.CONTAINER,
            PackagePurpose.FIRMWARE: ComponentType.FIRMWARE,
            PackagePurpose.OPERATING_SYSTEM: ComponentType.OPERATING_SYSTEM,
            PackagePurpose.DEVICE: ComponentType.DEVICE,
            PackagePurpose.FILE: ComponentType.FILE,
            PackagePurpose.OTHER: ComponentType.LIBRARY,
        }
        return mapping.get(purpose, ComponentType.LIBRARY)

    def reset(self) -> None:
        """
        Reset the adapter state.

        Call this when serializing multiple documents independently to
        ensure fresh bom-ref generation for each document.
        """
        self._id_cache.clear()
        self._name_counts.clear()
