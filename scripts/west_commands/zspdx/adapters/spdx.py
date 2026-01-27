# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
SPDX-specific adapters for ID generation and transformations.

This module provides the SPDXAdapter class, which handles the generation
of SPDX-compliant identifiers from the format-agnostic SBOM model.
"""

from __future__ import annotations

import os
import re
from typing import TYPE_CHECKING

from zspdx.model.base import ElementType

if TYPE_CHECKING:
    from zspdx.model.base import SBOMElement


class SPDXAdapter:
    """
    Adapter for SPDX-specific transformations.

    This adapter handles generation of SPDX IDs and other format-specific
    requirements that shouldn't be in the model layer. SPDX IDs are
    generated on-demand and cached for consistency.

    SPDX ID Format:
        - Document: SPDXRef-DOCUMENT
        - Package: SPDXRef-{safe-name}
        - File: SPDXRef-File-{safe-filename}[-{count}]

    SPDX ID Character Rules:
        - Must start with "SPDXRef-"
        - Remainder can only contain: letters, numbers, '.', '-'
        - Underscores are not allowed (replaced with dashes)
        - Other special characters are replaced with dashes

    Attributes:
        _id_cache: Internal cache mapping internal_id to generated SPDX ID.
        _filename_counts: Counter for unique file IDs per filename.

    Example:
        >>> from zspdx.adapters import SPDXAdapter
        >>> from zspdx.model import SBOMPackage, SBOMFile
        >>>
        >>> adapter = SPDXAdapter()
        >>> pkg = SBOMPackage(internal_id="pkg1", name="my_package")
        >>> adapter.get_spdx_id(pkg)
        'SPDXRef-my-package'
        >>>
        >>> file = SBOMFile(
        ...     internal_id="f1",
        ...     name="main.c",
        ...     absolute_path="/src/main.c"
        ... )
        >>> adapter.get_spdx_id(file)
        'SPDXRef-File-main.c'

    Note:
        A single adapter instance should be used for an entire document
        serialization to ensure consistent ID generation. Call reset()
        if you need to serialize multiple documents independently.
    """

    def __init__(self) -> None:
        """Initialize the adapter with empty caches."""
        # Cache of internal_id -> SPDX ID mappings
        self._id_cache: dict[str, str] = {}
        # Counter for unique file IDs per filename
        self._filename_counts: dict[str, int] = {}

    def get_spdx_id(self, element: SBOMElement) -> str:
        """
        Get or generate the SPDX ID for an element.

        This method returns a cached ID if one was already generated for
        this element, otherwise it generates a new ID, caches it, and
        returns it.

        Args:
            element: The SBOM element to get an ID for.

        Returns:
            The SPDX ID string (with SPDXRef- prefix).
        """
        if element.internal_id in self._id_cache:
            return self._id_cache[element.internal_id]

        spdx_id = self._generate_spdx_id(element)
        self._id_cache[element.internal_id] = spdx_id
        return spdx_id

    def _generate_spdx_id(self, element: SBOMElement) -> str:
        """
        Generate an SPDX ID for an element.

        The ID format depends on the element type:
        - Document: SPDXRef-DOCUMENT
        - Package: SPDXRef-{safe-name}
        - File: SPDXRef-File-{safe-filename}[-{count}]

        Args:
            element: The SBOM element to generate an ID for.

        Returns:
            The generated SPDX ID string.
        """
        if element.element_type == ElementType.DOCUMENT:
            return "SPDXRef-DOCUMENT"

        elif element.element_type == ElementType.PACKAGE:
            safe_name = self._convert_to_safe(element.name)
            return f"SPDXRef-{safe_name}"

        elif element.element_type == ElementType.FILE:
            # Import here to avoid circular imports
            from zspdx.model.file import SBOMFile

            file: SBOMFile = element  # type: ignore
            # Extract just the filename from the path
            filename = os.path.basename(file.absolute_path)
            safe_name = self._convert_to_safe(filename)
            return self._get_unique_file_id(safe_name)

        else:
            # Fallback for relationships or unknown types
            safe_id = self._convert_to_safe(element.internal_id)
            return f"SPDXRef-{safe_id}"

    def _convert_to_safe(self, s: str) -> str:
        """
        Convert a string to only SPDX-ID-safe characters.

        SPDX IDs can only contain: letters, numbers, '.', '-'
        All other characters are replaced with '-'.

        Args:
            s: The string to convert.

        Returns:
            String containing only SPDX-safe characters.
        """
        result = []
        for c in s:
            if c.isalnum() or c in ("-", "."):
                result.append(c)
            else:
                result.append("-")
        return "".join(result)

    def _get_unique_file_id(self, safe_filename: str) -> str:
        """
        Generate a unique file ID, handling duplicates.

        When multiple files have the same name (but different paths),
        a numeric suffix is added to ensure uniqueness.

        Special handling for filenames ending in -N (where N is a number):
        these get a -1 suffix to avoid ambiguity.

        Args:
            safe_filename: The SPDX-safe filename.

        Returns:
            Unique SPDX file ID.
        """
        count = self._filename_counts.get(safe_filename, 0) + 1
        self._filename_counts[safe_filename] = count

        spdx_id = f"SPDXRef-File-{safe_filename}"

        if count > 1:
            spdx_id += f"-{count}"
        else:
            # Handle edge case: filename ends in -number
            # This matches the behavior in the original spdxids.py
            if re.search(r"-\d+$", safe_filename):
                spdx_id += "-1"

        return spdx_id

    def reset(self) -> None:
        """
        Reset the adapter state.

        Call this when serializing multiple documents independently to
        ensure fresh ID generation for each document.
        """
        self._id_cache.clear()
        self._filename_counts.clear()
