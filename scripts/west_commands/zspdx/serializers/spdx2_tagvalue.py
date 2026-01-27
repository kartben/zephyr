# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
SPDX 2.x Tag-Value format serializer.

This module provides the SPDX2TagValueSerializer class, which produces
SPDX 2.x documents in tag-value format. Both SPDX 2.2 and 2.3 versions
are supported, with version-specific field handling.
"""

from __future__ import annotations

import re
from datetime import datetime
from typing import TYPE_CHECKING, TextIO

from packaging.version import Version
from west import log

from zspdx.adapters.spdx import SPDXAdapter
from zspdx.model.document import SBOMDocument
from zspdx.model.file import SBOMFile
from zspdx.model.package import PackagePurpose, SBOMPackage
from zspdx.model.relationship import RelationshipType, SBOMRelationship
from zspdx.serializers.base import Serializer, SerializerConfig
from zspdx.version import SPDX_VERSION_2_3

if TYPE_CHECKING:
    from zspdx.model.package import ExternalReference


class SPDX2TagValueSerializer(Serializer):
    """
    Serializer for SPDX 2.x Tag-Value format.

    This produces the traditional SPDX tag-value output format compatible
    with SPDX 2.2 and 2.3 specifications. The version is controlled via
    the SerializerConfig.version parameter.

    Version differences handled:
        - SPDX 2.3+: PrimaryPackagePurpose field is included
        - SPDX 2.2: PrimaryPackagePurpose field is omitted

    Attributes:
        config: SerializerConfig controlling serialization behavior.
        adapter: SPDXAdapter for generating format-specific IDs.

    Example:
        >>> from zspdx.serializers import SPDX2TagValueSerializer, SerializerConfig
        >>> from zspdx.model import SBOMDocument
        >>> from pathlib import Path
        >>>
        >>> config = SerializerConfig(version="2.3")
        >>> serializer = SPDX2TagValueSerializer(config)
        >>> doc = SBOMDocument(internal_id="my-doc", name="my-project")
        >>> # ... populate document ...
        >>> serializer.serialize_to_file(doc, Path("output.spdx"))
    """

    # Regex patterns for external reference validation
    CPE23_REGEX = re.compile(
        r"^cpe:2\.3:[aho\*\-](:(((\?*|\*?)([a-zA-Z0-9\-\._]|"
        r'(\\[\\\*\?!"#$$%&\'\(\)\+,\/:;<=>@\[\]\^`\{\|}~]))+(\?*|\*?))|'
        r"[\*\-])){5}(:(([a-zA-Z]{2,3}(-([a-zA-Z]{2}|[0-9]{3}))?)|[\*\-]))"
        r"(:(((\?*|\*?)([a-zA-Z0-9\-\._]|(\\[\\\*\?!\"#$$%&'\\(\\)\\+,\\/"
        r":;<=>@\\[\\]\\^`\\{\\|\\}~]))+(\?*|\*?))|[\*\-])){4}$"
    )
    PURL_REGEX = re.compile(r"^pkg:.+(\/.+)?\/.+(@.+)?(\?.+)?(#.+)?$")

    def __init__(self, config: SerializerConfig | None = None) -> None:
        """
        Initialize the SPDX 2.x tag-value serializer.

        Args:
            config: SerializerConfig controlling serialization behavior.
                The version field should be "2.2" or "2.3" (default: "2.3").
        """
        super().__init__(config)
        self.adapter = SPDXAdapter()
        self._version = Version(self.config.version) if self.config.version else SPDX_VERSION_2_3

    @property
    def format_name(self) -> str:
        """Return 'SPDX-{version}-TagValue'."""
        return f"SPDX-{self._version}-TagValue"

    @property
    def file_extension(self) -> str:
        """Return '.spdx'."""
        return ".spdx"

    def serialize(self, document: SBOMDocument, output: TextIO) -> None:
        """
        Serialize document to SPDX tag-value format.

        Args:
            document: The SBOMDocument to serialize.
            output: Text stream to write the output to.
        """
        log.inf(f"Writing SPDX {self._version} document {document.name}")
        self._write_document_header(document, output)
        self._write_external_document_refs(document, output)
        self._write_document_relationships(document, output)
        self._write_packages(document, output)
        self._write_custom_licenses(document, output)

    def _normalize_name(self, name: str) -> str:
        """
        Replace underscores with dashes for SPDX ID compliance.

        SPDX IDs cannot contain underscores, so they are replaced with dashes.

        Args:
            name: The name to normalize.

        Returns:
            Normalized name with underscores replaced by dashes.
        """
        return name.replace("_", "-")

    def _write_document_header(self, doc: SBOMDocument, output: TextIO) -> None:
        """Write the SPDX document header section."""
        normalized_name = self._normalize_name(doc.metadata.name)
        output.write(
            f"""SPDXVersion: SPDX-{self._version}
DataLicense: CC0-1.0
SPDXID: SPDXRef-DOCUMENT
DocumentName: {normalized_name}
DocumentNamespace: {doc.metadata.namespace_prefix}
Creator: Tool: {doc.metadata.creator_tool}
Created: {datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")}

"""
        )

    def _write_external_document_refs(self, doc: SBOMDocument, output: TextIO) -> None:
        """Write external document references section."""
        if not doc.external_documents:
            return

        ext_docs = sorted(doc.external_documents, key=lambda x: x.metadata.doc_ref_id)
        for ext_doc in ext_docs:
            output.write(
                f"ExternalDocumentRef: {ext_doc.metadata.doc_ref_id} "
                f"{ext_doc.metadata.namespace_prefix} SHA1: {ext_doc.document_hash}\n"
            )
        output.write("\n")

    def _write_document_relationships(self, doc: SBOMDocument, output: TextIO) -> None:
        """Write relationships owned by the document."""
        if not doc.relationships:
            return

        for rln in doc.relationships:
            self._write_relationship(rln, doc, output)
        output.write("\n")

    def _write_packages(self, doc: SBOMDocument, output: TextIO) -> None:
        """Write all packages in the document."""
        for pkg in doc.packages.values():
            self._write_package(pkg, doc, output)

    def _write_package(self, pkg: SBOMPackage, doc: SBOMDocument, output: TextIO) -> None:
        """Write a single package section."""
        spdx_id = self.adapter.get_spdx_id(pkg)
        normalized_name = self._normalize_name(pkg.name)

        # Check external references for CPE that might override metadata
        name = pkg.name
        supplier = pkg.supplier
        version = pkg.version
        for ref in pkg.external_references:
            if self.CPE23_REGEX.fullmatch(ref.locator):
                metadata = ref.locator.split(":", 6)
                # metadata: [cpe, 2.3, a, vendor, product, version, ...]
                if len(metadata) >= 6:
                    supplier = metadata[3]
                    name = metadata[4]
                    version = metadata[5]
                break

        output.write(f"##### Package: {normalized_name}\n\n")
        output.write(f"PackageName: {name}\n")
        output.write(f"SPDXID: {self._normalize_name(spdx_id)}\n")
        output.write(f"PackageLicenseConcluded: {pkg.concluded_license}\n")
        output.write(f"PackageLicenseDeclared: {pkg.declared_license}\n")
        output.write(f"PackageCopyrightText: {pkg.copyright_text}\n")

        # PrimaryPackagePurpose only in SPDX 2.3+
        if self._version >= SPDX_VERSION_2_3 and pkg.purpose:
            purpose_str = self._map_purpose(pkg.purpose)
            if purpose_str:
                output.write(f"PrimaryPackagePurpose: {purpose_str}\n")

        # Download location
        if pkg.download_url:
            url = self._format_download_url(pkg.download_url, pkg.revision)
            output.write(f"PackageDownloadLocation: {url}\n")
        else:
            output.write("PackageDownloadLocation: NOASSERTION\n")

        # Version
        if version:
            output.write(f"PackageVersion: {version}\n")
        elif pkg.revision:
            output.write(f"PackageVersion: {pkg.revision}\n")

        # Supplier
        if supplier:
            output.write(f"PackageSupplier: Organization: {supplier}\n")

        # External references
        for ref in pkg.external_references:
            self._write_external_reference(ref, output)

        # Files analyzed
        if pkg.has_files:
            if pkg.license_info_from_files:
                for lic in pkg.license_info_from_files:
                    output.write(f"PackageLicenseInfoFromFiles: {lic}\n")
            else:
                output.write("PackageLicenseInfoFromFiles: NOASSERTION\n")
            output.write("FilesAnalyzed: true\n")
            output.write(f"PackageVerificationCode: {pkg.verification_code}\n\n")
        else:
            output.write("FilesAnalyzed: false\n")
            output.write("PackageComment: Utility target; no files\n\n")

        # Package relationships
        for rln in pkg.relationships:
            self._write_relationship(rln, doc, output)
        if pkg.relationships:
            output.write("\n")

        # Files
        if pkg.has_files:
            files = sorted(pkg.files.values(), key=lambda f: f.relative_path)
            for file in files:
                self._write_file(file, doc, output)

    def _write_file(self, file: SBOMFile, doc: SBOMDocument, output: TextIO) -> None:
        """Write a single file section."""
        spdx_id = self.adapter.get_spdx_id(file)

        output.write(f"FileName: ./{file.relative_path}\n")
        output.write(f"SPDXID: {self._normalize_name(spdx_id)}\n")
        output.write(f"FileChecksum: SHA1: {file.hashes.sha1}\n")

        if file.hashes.sha256:
            output.write(f"FileChecksum: SHA256: {file.hashes.sha256}\n")
        if file.hashes.md5:
            output.write(f"FileChecksum: MD5: {file.hashes.md5}\n")

        output.write(f"LicenseConcluded: {file.concluded_license}\n")

        if file.license_info_in_file:
            for lic in file.license_info_in_file:
                output.write(f"LicenseInfoInFile: {lic}\n")
        else:
            output.write("LicenseInfoInFile: NONE\n")

        output.write(f"FileCopyrightText: {file.copyright_text}\n\n")

        # File relationships
        for rln in file.relationships:
            self._write_relationship(rln, doc, output)
        if file.relationships:
            output.write("\n")

    def _write_relationship(
        self,
        rln: SBOMRelationship,
        owner_doc: SBOMDocument,
        output: TextIO,
    ) -> None:
        """Write a single relationship."""
        if rln.source is None or rln.target is None:
            return

        source_id = self.adapter.get_spdx_id(rln.source)
        target_id = self.adapter.get_spdx_id(rln.target)

        # Add external document reference prefix if needed
        if rln.external_document and rln.external_document != owner_doc:
            target_id = f"{rln.external_document.metadata.doc_ref_id}:{target_id}"
            owner_doc.external_documents.add(rln.external_document)

        rln_type = self._map_relationship_type(rln.relationship_type)
        source_id = self._normalize_name(source_id)
        target_id = self._normalize_name(target_id)

        output.write(f"Relationship: {source_id} {rln_type} {target_id}\n")

    def _write_custom_licenses(self, doc: SBOMDocument, output: TextIO) -> None:
        """Write custom license declarations."""
        for lic in sorted(doc.custom_license_ids):
            output.write(
                f"""LicenseID: {lic}
ExtractedText: {lic}
LicenseName: {lic}
LicenseComment: Corresponds to the license ID `{lic}` detected in an SPDX-License-Identifier: tag.
"""
            )

    def _write_external_reference(self, ref: ExternalReference, output: TextIO) -> None:
        """Write an external reference."""
        if self.CPE23_REGEX.fullmatch(ref.locator):
            output.write(f"ExternalRef: SECURITY cpe23Type {ref.locator}\n")
        elif self.PURL_REGEX.fullmatch(ref.locator):
            output.write(f"ExternalRef: PACKAGE-MANAGER purl {ref.locator}\n")
        else:
            log.wrn(f"Unknown external reference ({ref.locator})")

    def _map_purpose(self, purpose: PackagePurpose) -> str:
        """
        Map internal purpose enum to SPDX string.

        Args:
            purpose: The PackagePurpose enum value.

        Returns:
            SPDX PrimaryPackagePurpose string, or empty if not mappable.
        """
        mapping = {
            PackagePurpose.APPLICATION: "APPLICATION",
            PackagePurpose.LIBRARY: "LIBRARY",
            PackagePurpose.SOURCE: "SOURCE",
            PackagePurpose.FRAMEWORK: "FRAMEWORK",
            PackagePurpose.CONTAINER: "CONTAINER",
            PackagePurpose.FIRMWARE: "FIRMWARE",
            PackagePurpose.OPERATING_SYSTEM: "OPERATING-SYSTEM",
            PackagePurpose.DEVICE: "DEVICE",
            PackagePurpose.FILE: "FILE",
        }
        return mapping.get(purpose, "")

    def _map_relationship_type(self, rln_type: RelationshipType) -> str:
        """
        Map internal relationship type to SPDX string.

        Args:
            rln_type: The RelationshipType enum value.

        Returns:
            SPDX relationship type string.
        """
        mapping = {
            RelationshipType.DESCRIBES: "DESCRIBES",
            RelationshipType.GENERATED_FROM: "GENERATED_FROM",
            RelationshipType.DERIVED_FROM: "DERIVED_FROM",
            RelationshipType.ANCESTOR_OF: "ANCESTOR_OF",
            RelationshipType.DESCENDANT_OF: "DESCENDANT_OF",
            RelationshipType.DEPENDS_ON: "DEPENDS_ON",
            RelationshipType.DEPENDENCY_OF: "DEPENDENCY_OF",
            RelationshipType.HAS_PREREQUISITE: "HAS_PREREQUISITE",
            RelationshipType.PREREQUISITE_OF: "PREREQUISITE_OF",
            RelationshipType.CONTAINS: "CONTAINS",
            RelationshipType.CONTAINED_BY: "CONTAINED_BY",
            RelationshipType.BUILD_TOOL_OF: "BUILD_TOOL_OF",
            RelationshipType.STATIC_LINK: "STATIC_LINK",
            RelationshipType.DYNAMIC_LINK: "DYNAMIC_LINK",
            RelationshipType.OTHER: "OTHER",
        }
        return mapping.get(rln_type, "OTHER")

    def _format_download_url(self, url: str, revision: str) -> str:
        """
        Format download URL with revision if present.

        For git repositories, this prepends "git+" and appends "@{revision}".

        Args:
            url: The base download URL.
            revision: The revision (e.g., git commit hash or tag).

        Returns:
            Formatted download URL.
        """
        if revision:
            return f"git+{url}@{revision}"
        return url
