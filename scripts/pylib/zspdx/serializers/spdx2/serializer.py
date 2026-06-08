# Copyright (c) 2020-2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import logging
import os
import re
from datetime import UTC, datetime

from license_expression import get_spdx_licensing
from spdx_tools.spdx.model import (
    Actor,
    ActorType,
    Checksum,
    ChecksumAlgorithm,
    CreationInfo,
    Document,
    ExternalDocumentRef,
    ExternalPackageRef,
    ExternalPackageRefCategory,
    ExtractedLicensingInfo,
    File,
    Package,
    PackagePurpose,
    PackageVerificationCode,
    Relationship,
    RelationshipType,
    SpdxNoAssertion,
    SpdxNone,
)
from spdx_tools.spdx.writer.write_anything import write_file

from zspdx.model import ComponentPurpose, SBOMComponent, SBomDocument, SBOMFile
from zspdx.serializers.helpers import (
    CPE23TYPE_REGEX,
    PURL_REGEX,
    generate_download_url,
    get_standard_licenses,
    normalize_spdx_name,
)
from zspdx.util import getHashes
from zspdx.version import SPDX_VERSION_2_3

_logger = logging.getLogger(__name__)

# File extension for each supported output format
_FORMAT_EXTENSIONS = {
    "tag-value": "spdx",
    "json": "spdx.json",
}


class SPDX2Serializer:
    """Serializer that converts SBOMData to SPDX 2.x documents.

    The format-agnostic SBOMData is mapped onto the ``spdx_tools`` SPDX 2.x data
    model and written out with ``spdx_tools.writer.write_anything.write_file``,
    which selects the concrete output format (tag-value or JSON) from the file
    extension and validates the result.
    """

    def __init__(self, sbom_data, spdx_version=SPDX_VERSION_2_3, spdx_format="tag-value"):
        self.sbom_data = sbom_data
        self.spdx_version = spdx_version
        if spdx_format not in _FORMAT_EXTENSIONS:
            raise ValueError(f"Unsupported SPDX 2.x output format: {spdx_format}")
        self.spdx_format = spdx_format

        # Track generated IDs
        self.component_ids = {}  # component_name -> SPDX ID
        self.file_ids = {}  # file_path -> SPDX ID
        self.document_refs = {}  # document_name -> DocumentRef ID

    def serialize(self, output_dir):
        """Serialize SBOMData to SPDX 2.x format files."""
        # Generate IDs for all components and files
        self._generate_ids()

        # Build the spdx_tools document model for every non-empty document.
        models = {}  # doc_name -> (SBomDocument, spdx_tools Document, output_path)
        for doc in self.sbom_data.documents.values():
            if not doc.components:
                continue
            model = self._build_document(doc)
            filename = f"{doc.name}.{_FORMAT_EXTENSIONS[self.spdx_format]}"
            output_path = os.path.join(output_dir, filename)
            models[doc.name] = (doc, model, output_path)

        # First pass: write each document without external document references so
        # we can compute its checksum (mirrors the historic two-pass behavior).
        for doc, model, output_path in models.values():
            if not self._write_model(model, output_path, validate=False):
                return False
            hashes = getHashes(output_path)
            if not hashes:
                _logger.error(f"Unable to calculate hash values for {output_path}")
                return False
            doc.doc_hash = hashes[0]

        # Second pass: add external document references (now that every referenced
        # document has a checksum) and rewrite, validating the final output.
        for doc, model, output_path in models.values():
            self._add_external_document_refs(model, doc)
            if not self._write_model(model, output_path, validate=True):
                return False

        return True

    def _write_model(self, model, output_path, validate):
        """Write a spdx_tools Document to disk in the configured format."""
        try:
            _logger.info(f"Writing SPDX {self.spdx_version} document to {output_path}")
            write_file(model, output_path, validate=validate)
            return True
        except Exception as e:  # noqa: BLE001 - surface any validation/IO failure
            _logger.error(f"Failed to write SPDX document {output_path}: {e}")
            return False

    def _generate_ids(self):
        """Generate SPDX IDs for all components and files."""
        # Generate component IDs
        for component in self.sbom_data.components.values():
            spdx_id = f"SPDXRef-{normalize_spdx_name(component.name)}"
            self.component_ids[component.name] = spdx_id

        # Generate file IDs (tracking filename counts for uniqueness)
        filename_counts = {}
        for file_path, _ in self.sbom_data.files.items():
            filename_only = os.path.basename(file_path)
            safe_name = normalize_spdx_name(filename_only)
            count = filename_counts.get(safe_name, 0) + 1
            filename_counts[safe_name] = count

            if count == 1:
                spdx_id = f"SPDXRef-File-{safe_name}"
            else:
                spdx_id = f"SPDXRef-File-{safe_name}-{count}"

            self.file_ids[file_path] = spdx_id

        # Generate document reference IDs
        for doc_name in self.sbom_data.documents:
            self.document_refs[doc_name] = f"DocumentRef-{doc_name}"

    # -- model builders ------------------------------------------------------

    def _build_document(self, doc: SBomDocument) -> Document:
        """Build a spdx_tools Document from a format-agnostic SBomDocument."""
        namespace = doc.namespace or f"{self.sbom_data.namespace_prefix}/{doc.name}"
        creation_info = CreationInfo(
            spdx_version=f"SPDX-{self.spdx_version}",
            spdx_id="SPDXRef-DOCUMENT",
            name=normalize_spdx_name(doc.name),
            document_namespace=namespace,
            creators=[Actor(ActorType.TOOL, "Zephyr SPDX builder")],
            created=datetime.now(UTC).replace(microsecond=0, tzinfo=None),
            data_license="CC0-1.0",
        )

        packages = []
        files = []
        relationships = []

        # DESCRIBES relationships from the document to each top-level package.
        for component in doc.components.values():
            relationships.append(
                Relationship(
                    "SPDXRef-DOCUMENT",
                    RelationshipType.DESCRIBES,
                    self.component_ids[component.name],
                )
            )

        for component in doc.components.values():
            packages.append(self._build_package(component))

            # Files belong to the document (flat); containment is expressed with
            # explicit CONTAINS relationships (the tag-value parser otherwise
            # infers these from inline grouping, but JSON has no such grouping).
            pkg_id = self.component_ids[component.name]
            for file_obj in component.files.values():
                files.append(self._build_file(file_obj))
                relationships.append(
                    Relationship(pkg_id, RelationshipType.CONTAINS, self.file_ids[file_obj.path])
                )

            # Component-level relationships (e.g. STATIC_LINK, GENERATED_FROM).
            for rel in component.relationships:
                relationships.extend(
                    self._relationship_objects(rel, component.name, "component", doc)
                )

            # File-level relationships (e.g. GENERATED_FROM source files).
            for file_obj in component.files.values():
                for rel in file_obj.relationships:
                    relationships.extend(
                        self._relationship_objects(rel, file_obj.path, "file", doc)
                    )

        return Document(
            creation_info=creation_info,
            packages=packages,
            files=files,
            relationships=relationships,
            extracted_licensing_info=self._build_extracted_licensing_info(doc),
        )

    def _build_package(self, component: SBOMComponent) -> Package:
        """Build a spdx_tools Package from an SBOMComponent."""
        spdx_id = self.component_ids[component.name]

        # Derive name/supplier/version from a CPE reference when present, without
        # mutating the component (its name is the ID lookup key).
        package_name = component.name
        supplier = component.supplier
        package_version = component.version
        for ref in component.external_references:
            if re.fullmatch(CPE23TYPE_REGEX, ref):
                metadata = ref.split(':', 6)
                # metadata: [cpe,2.3,a,arm,mbed_tls,3.5.1,*:*:*:*:*:*]
                if len(metadata) > 5:
                    supplier = supplier if supplier else metadata[3]
                    package_name = metadata[4] if len(metadata) > 4 else package_name
                    package_version = (
                        metadata[5]
                        if len(metadata) > 5 and not package_version
                        else package_version
                    )

        if component.url:
            download_location = self._noassertion_or_value(
                generate_download_url(component.url, component.revision)
            )
        else:
            download_location = SpdxNoAssertion()

        kwargs = {
            "spdx_id": spdx_id,
            "name": package_name,
            "download_location": download_location,
            "license_concluded": self._to_license(component.concluded_license),
            "license_declared": self._to_license(component.declared_license),
            "copyright_text": self._to_copyright(component.copyright_text),
        }

        version = package_version or component.revision
        if version:
            kwargs["version"] = version

        if supplier:
            kwargs["supplier"] = Actor(ActorType.ORGANIZATION, supplier)

        # PrimaryPackagePurpose is only available in SPDX 2.3 and later.
        if self.spdx_version >= SPDX_VERSION_2_3:
            purpose = self._purpose_to_spdx(component.purpose)
            if purpose:
                kwargs["primary_package_purpose"] = purpose

        ext_refs = []
        for ref in component.external_references:
            if re.fullmatch(CPE23TYPE_REGEX, ref):
                ext_refs.append(
                    ExternalPackageRef(ExternalPackageRefCategory.SECURITY, "cpe23Type", ref)
                )
            elif re.fullmatch(PURL_REGEX, ref):
                ext_refs.append(
                    ExternalPackageRef(ExternalPackageRefCategory.PACKAGE_MANAGER, "purl", ref)
                )
            else:
                _logger.warning(f"Unknown external reference ({ref})")
        if ext_refs:
            kwargs["external_references"] = ext_refs

        if len(component.files) > 0:
            kwargs["files_analyzed"] = True
            kwargs["verification_code"] = PackageVerificationCode(
                value=component.verification_code
            )
            if component.license_info_from_files:
                kwargs["license_info_from_files"] = [
                    self._to_license(lic) for lic in component.license_info_from_files
                ]
            else:
                kwargs["license_info_from_files"] = [SpdxNoAssertion()]
        else:
            kwargs["files_analyzed"] = False
            kwargs["comment"] = "Utility target; no files"

        return Package(**kwargs)

    def _build_file(self, file_obj: SBOMFile) -> File:
        """Build a spdx_tools File from an SBOMFile."""
        checksums = [Checksum(ChecksumAlgorithm.SHA1, file_obj.hashes.get('SHA1', ''))]
        if file_obj.hashes.get('SHA256'):
            checksums.append(Checksum(ChecksumAlgorithm.SHA256, file_obj.hashes['SHA256']))
        if file_obj.hashes.get('MD5'):
            checksums.append(Checksum(ChecksumAlgorithm.MD5, file_obj.hashes['MD5']))

        if file_obj.license_info_in_file:
            license_info = [self._to_license(lic) for lic in file_obj.license_info_in_file]
        else:
            license_info = [SpdxNone()]

        return File(
            name=f"./{file_obj.relative_path}",
            spdx_id=self.file_ids[file_obj.path],
            checksums=checksums,
            license_concluded=self._to_license(file_obj.concluded_license),
            license_info_in_file=license_info,
            copyright_text=self._to_copyright(file_obj.copyright_text),
        )

    def _relationship_objects(self, rel, from_identifier, from_type, current_doc: SBomDocument):
        """Build spdx_tools Relationship objects from an SBOMRelationship."""
        out = []
        if from_type == "component":
            from_id = self.component_ids.get(from_identifier, "")
        else:  # file
            from_id = self.file_ids.get(from_identifier, "")

        if not from_id:
            return out

        rel_type = self._to_relationship_type(rel.relationship_type)

        to_elements = rel.to_elements if isinstance(rel.to_elements, list) else [rel.to_elements]
        for to_elem in to_elements:
            to_id = None
            doc_ref = None

            if isinstance(to_elem, SBOMComponent):
                to_id = self.component_ids.get(to_elem.name, "")
                to_doc_name = self._get_document_name_for_component(to_elem.name)
                if to_doc_name and to_doc_name != current_doc.name:
                    doc_ref = self.document_refs.get(to_doc_name, "")
            elif isinstance(to_elem, SBOMFile):
                to_id = self.file_ids.get(to_elem.path, "")
                if to_elem.component:
                    to_doc_name = self._get_document_name_for_component(to_elem.component.name)
                    if to_doc_name and to_doc_name != current_doc.name:
                        doc_ref = self.document_refs.get(to_doc_name, "")

            if to_id:
                if doc_ref:
                    to_id = f"{doc_ref}:{to_id}"
                out.append(Relationship(from_id, rel_type, to_id))

        return out

    def _add_external_document_refs(self, model: Document, doc: SBomDocument):
        """Populate the model's external document references from computed hashes."""
        ext_refs = []
        for ext_doc in doc.external_documents.values():
            if not ext_doc.doc_hash:  # Only include if hash has been calculated
                continue
            doc_ref_id = self.document_refs.get(ext_doc.name, f"DocumentRef-{ext_doc.name}")
            namespace = ext_doc.namespace or f"{self.sbom_data.namespace_prefix}/{ext_doc.name}"
            ext_refs.append(
                ExternalDocumentRef(
                    doc_ref_id, namespace, Checksum(ChecksumAlgorithm.SHA1, ext_doc.doc_hash)
                )
            )
        ext_refs.sort(key=lambda r: r.document_ref_id)
        model.creation_info.external_document_refs = ext_refs

    def _build_extracted_licensing_info(self, doc: SBomDocument):
        """Collect custom (non-standard) licenses as ExtractedLicensingInfo entries."""
        custom_licenses = set()
        standard_licenses = get_standard_licenses()
        for component in doc.components.values():
            for file_obj in component.files.values():
                for lic in file_obj.license_info_in_file:
                    if lic not in standard_licenses:
                        custom_licenses.add(lic)

        # Also include any custom licenses stored in the document
        custom_licenses.update(doc.custom_license_ids)

        return [
            ExtractedLicensingInfo(
                license_id=lic,
                extracted_text=lic,
                license_name=lic,
                comment=(
                    f"Corresponds to the license ID `{lic}` detected in an "
                    "SPDX-License-Identifier: tag."
                ),
            )
            for lic in sorted(custom_licenses)
        ]

    # -- small helpers -------------------------------------------------------

    def _get_document_name_for_component(self, component_name: str) -> str:
        """Get document name for a component."""
        for doc in self.sbom_data.documents.values():
            if component_name in doc.components:
                return doc.name
        return None

    def _to_relationship_type(self, value: str) -> RelationshipType:
        """Map a relationship-type string to the spdx_tools enum."""
        try:
            return RelationshipType[value]
        except KeyError:
            _logger.warning(f"Unknown relationship type '{value}'; using OTHER")
            return RelationshipType.OTHER

    def _to_license(self, value):
        """Convert a license string to a spdx_tools license value."""
        if not value or value == "NOASSERTION":
            return SpdxNoAssertion()
        if value == "NONE":
            return SpdxNone()
        try:
            return get_spdx_licensing().parse(value)
        except Exception:  # noqa: BLE001 - fall back to NOASSERTION on unparseable input
            _logger.warning(f"Could not parse license expression '{value}'; using NOASSERTION")
            return SpdxNoAssertion()

    def _to_copyright(self, value):
        """Convert a copyright string to a spdx_tools copyright value."""
        if value is None or value == "NOASSERTION":
            return SpdxNoAssertion()
        if value == "NONE":
            return SpdxNone()
        return value

    def _noassertion_or_value(self, value):
        """Map the NOASSERTION/NONE sentinels to their spdx_tools singletons."""
        if value is None or value == "NOASSERTION":
            return SpdxNoAssertion()
        if value == "NONE":
            return SpdxNone()
        return value

    def _purpose_to_spdx(self, purpose):
        """Convert ComponentPurpose enum to spdx_tools PackagePurpose."""
        purpose_map = {
            ComponentPurpose.APPLICATION: PackagePurpose.APPLICATION,
            ComponentPurpose.LIBRARY: PackagePurpose.LIBRARY,
            ComponentPurpose.SOURCE: PackagePurpose.SOURCE,
            ComponentPurpose.FILE: PackagePurpose.FILE,
        }
        return purpose_map.get(purpose)
