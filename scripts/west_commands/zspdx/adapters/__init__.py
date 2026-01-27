# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
SBOM format adapters package.

This package provides adapters that transform the format-agnostic SBOM model
into format-specific representations. Adapters handle concerns like ID
generation, validation, and format-specific transformations.

Currently available adapters:
    - SPDXAdapter: Generates SPDX-compliant identifiers (SPDXRef-*)

Future adapters (not yet implemented):
    - CycloneDXAdapter: Generates CycloneDX bom-ref identifiers

Example:
    >>> from zspdx.adapters import SPDXAdapter
    >>> from zspdx.model import SBOMPackage
    >>>
    >>> adapter = SPDXAdapter()
    >>> pkg = SBOMPackage(internal_id="kernel", name="zephyr_kernel")
    >>> spdx_id = adapter.get_spdx_id(pkg)
    >>> spdx_id
    'SPDXRef-zephyr-kernel'
"""

from zspdx.adapters.spdx import SPDXAdapter

__all__ = [
    "SPDXAdapter",
]
