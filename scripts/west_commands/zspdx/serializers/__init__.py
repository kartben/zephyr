# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
SBOM serialization package.

This package provides serializers that transform the format-agnostic SBOM
model into specific output formats. Each serializer implements the abstract
Serializer interface, ensuring consistent behavior across formats.

Currently supported formats:
    - SPDX 2.x Tag-Value: Traditional SPDX text format (2.2 and 2.3)

Future formats (not yet implemented):
    - SPDX 2.x JSON
    - SPDX 3.x JSON-LD
    - CycloneDX JSON/XML

Example:
    >>> from zspdx.model import SBOMDocument
    >>> from zspdx.serializers import SPDX2TagValueSerializer, SerializerConfig
    >>> from pathlib import Path
    >>>
    >>> doc = SBOMDocument(internal_id="my-sbom", name="My SBOM")
    >>> # ... populate document with packages and files ...
    >>>
    >>> config = SerializerConfig(version="2.3")
    >>> serializer = SPDX2TagValueSerializer(config)
    >>> doc_hash = serializer.serialize_to_file(doc, Path("output.spdx"))
"""

from zspdx.serializers.base import Serializer, SerializerConfig
from zspdx.serializers.spdx2_tagvalue import SPDX2TagValueSerializer

__all__ = [
    "Serializer",
    "SerializerConfig",
    "SPDX2TagValueSerializer",
]
