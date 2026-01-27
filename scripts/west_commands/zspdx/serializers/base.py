# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
Abstract base class for SBOM serializers.

This module defines the Serializer abstract base class that all format-specific
serializers must implement. This ensures consistent behavior and interface
across different SBOM output formats.
"""

from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from pathlib import Path
from typing import TextIO

from zspdx.model.document import SBOMDocument
from zspdx.util import getHashes


@dataclass
class SerializerConfig:
    """
    Configuration for serialization.

    This class holds settings that control serialization behavior. Different
    serializers may interpret these settings differently based on format
    requirements.

    Attributes:
        version: Format version to use (e.g., "2.2", "2.3" for SPDX).
            The serializer will use format-appropriate defaults if empty.
        include_sha256: Whether to include SHA-256 hashes in output.
            Recommended for security but increases output size.
            Defaults to True.
        include_md5: Whether to include MD5 hashes in output.
            Generally not recommended for security use.
            Defaults to False.
        extra_options: Additional format-specific options as key-value pairs.
            Serializers can define their own options here.

    Example:
        >>> config = SerializerConfig(
        ...     version="2.3",
        ...     include_sha256=True,
        ...     include_md5=False
        ... )
    """

    version: str = ""
    include_sha256: bool = True
    include_md5: bool = False
    extra_options: dict = field(default_factory=dict)


class Serializer(ABC):
    """
    Abstract base class for SBOM format serializers.

    Serializers transform the format-agnostic SBOM model into specific
    output formats (SPDX tag-value, SPDX JSON, CycloneDX, etc.).

    All serializers must implement format_name, file_extension, and serialize.
    This ensures consistent behavior and allows code to work with any
    serializer without knowing the specific format.

    The serialize_to_file method is provided as a convenience that handles
    file I/O and hash computation.

    Attributes:
        config: SerializerConfig controlling serialization behavior.

    Example:
        >>> from zspdx.serializers import SPDX2TagValueSerializer, SerializerConfig
        >>> config = SerializerConfig(version="2.3")
        >>> serializer = SPDX2TagValueSerializer(config)
        >>> serializer.format_name
        'SPDX-2.3-TagValue'
        >>> serializer.file_extension
        '.spdx'

    Note:
        Subclasses must implement format_name, file_extension, and serialize.
        The serialize_to_file method uses serialize internally and should
        not typically need to be overridden.
    """

    def __init__(self, config: SerializerConfig | None = None) -> None:
        """
        Initialize the serializer with optional configuration.

        Args:
            config: SerializerConfig controlling serialization behavior.
                If None, default configuration is used.
        """
        self.config = config or SerializerConfig()

    @property
    @abstractmethod
    def format_name(self) -> str:
        """
        Return the name of this format.

        This should be a human-readable string identifying the format
        and version, e.g., 'SPDX-2.3-TagValue', 'CycloneDX-1.5-JSON'.

        Returns:
            Format name string.
        """

    @property
    @abstractmethod
    def file_extension(self) -> str:
        """
        Return the file extension for this format.

        This should include the leading dot, e.g., '.spdx', '.json', '.xml'.

        Returns:
            File extension string.
        """

    @abstractmethod
    def serialize(self, document: SBOMDocument, output: TextIO) -> None:
        """
        Serialize the document to the output stream.

        This is the core serialization method that subclasses must implement.
        It should write the complete serialized document to the provided
        text stream.

        Args:
            document: The SBOMDocument to serialize.
            output: A text stream to write the output to. The stream should
                be opened in text mode with appropriate encoding (typically UTF-8).

        Raises:
            ValueError: If the document is invalid or cannot be serialized.
        """

    def serialize_to_file(self, document: SBOMDocument, path: Path) -> str:
        """
        Serialize the document to a file and return its hash.

        This convenience method handles file I/O, calls serialize() to
        produce the output, and computes the SHA-1 hash of the written file.
        The hash is also stored in the document's document_hash attribute.

        Args:
            document: The SBOMDocument to serialize.
            path: Path where the output file should be written.

        Returns:
            SHA-1 hash of the written file as a lowercase hex string.
            This hash can be used by other documents that reference this one.

        Raises:
            OSError: If the file cannot be written.
            ValueError: If the document is invalid or cannot be serialized.
        """
        with open(path, "w", encoding="utf-8") as f:
            self.serialize(document, f)

        # Calculate hash of written file
        hashes = getHashes(str(path))
        if hashes:
            document.document_hash = hashes[0]
        return document.document_hash
