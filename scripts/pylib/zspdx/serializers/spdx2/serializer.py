# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

import json
import logging
import os
import tempfile
from datetime import UTC, datetime

from spdx_tools.spdx.model import (
    Actor,
    ActorType,
    Checksum,
    ChecksumAlgorithm,
    CreationInfo,
    Document,
    ExternalDocumentRef,
    ExtractedLicensingInfo,
    File,
    Package,
    Relationship,
    RelationshipType,
)
from spdx_tools.spdx.model.spdx_no_assertion import SpdxNoAssertion
from spdx_tools.spdx.writer.write_anything import write_file

from zspdx.model import (
    ExternalReferenceType,
    SBOMComponent,
    SBOMDocument,
    SBOMElement,
    SBOMFile,
)
from zspdx.serializers.helpers import (
    generate_download_url,
    get_standard_licenses,
    normalize_spdx_name,
)
from zspdx.serializers.spdx2 import model_mapping as mm
from zspdx.util import getHashes
from zspdx.version import SPDX_VERSION_2_3

_logger = logging.getLogger(__name__)

# Base name used for bundled (single-file) output.
_SINGLE_FILE_BASENAME = "sbom"


class SPDX2Serializer:
    """Serializer that converts SBOMGraph to SPDX 2.x documents using spdx-tools.

    Supports tag-value and JSON output (``output_format``), as one file per
    document (default) or all documents bundled into a single file
    (``single_file``).
    """

    def __init__(
        self,
        sbom_graph,
        spdx_version=SPDX_VERSION_2_3,
        output_format="tag-value",
        single_file=False,
    ):
        self.sbom_graph = sbom_graph
        self.spdx_version = spdx_version
        self.output_format = output_format
        self.single_file = single_file

        # Generated SPDX IDs / references
        self.component_ids = {}  # component_name -> SPDX ID
        self.file_ids = {}  # file_path -> SPDX ID
        self.document_refs = {}  # document_name -> DocumentRef ID
        self.document_hashes = {}  # document_name -> SHA1 hash

        # Creation timestamp, fixed once per run so a document's bytes are stable
        # across the two write passes (keeps ExternalDocumentRef SHA1s consistent).
        self._created = None

    # ------------------------------------------------------------------ #
    # Top-level orchestration
    # ------------------------------------------------------------------ #

    def serialize(self, output_dir):
        """Serialize the SBOMGraph to SPDX 2.x files in ``output_dir``."""
        try:
            self._generate_ids()
            self._created = datetime.now(UTC).replace(microsecond=0)

            if self.single_file:
                return self._serialize_single_file(output_dir)
            return self._serialize_split(output_dir)
        except Exception:
            _logger.exception("Failed to create SPDX 2.x output")
            return False

    def _documents(self):
        """Yield (name, SBOMDocument) for every document that has components."""
        return [
            (doc.name, doc)
            for doc in self.sbom_graph.documents.values()
            if doc.components
        ]

    def _output_extension(self):
        return "spdx" if self.output_format == "tag-value" else "spdx.json"

    def _serialize_split(self, output_dir):
        """Write one file per document, with cross-document references.

        Two passes are required because each ExternalDocumentRef carries the SHA1
        of the referenced document file, which only exists once it is written.
        """
        ext = self._output_extension()

        # Pass 1: write each document without external refs and capture its hash.
        written = {}
        for name, doc in self._documents():
            document = self._build_document(doc, with_external_refs=False)
            path = os.path.join(output_dir, f"{name}.{ext}")
            if not self._write_document(document, path):
                return False
            doc_hash = self._hash_file(path)
            if doc_hash is None:
                return False
            self.document_hashes[name] = doc_hash
            written[name] = path

        # Pass 2: rewrite each document, now including external document refs.
        for name, doc in self._documents():
            document = self._build_document(doc, with_external_refs=True)
            if not self._write_document(document, written[name]):
                return False

        _logger.info("SPDX %s documents written to %s", self.spdx_version, output_dir)
        return True

    def _serialize_single_file(self, output_dir):
        """Bundle all documents into a single output file.

        Cross-document references are preserved; their ExternalDocumentRef
        checksums are computed against each document's standalone serialization
        (there is no separate per-document file in this mode).
        """
        ext = self._output_extension()

        # Capture per-document hashes from a standalone (no external ref) render.
        with tempfile.TemporaryDirectory() as tmp:
            for name, doc in self._documents():
                document = self._build_document(doc, with_external_refs=False)
                tmp_path = os.path.join(tmp, f"{name}.{ext}")
                if not self._write_document(document, tmp_path):
                    return False
                doc_hash = self._hash_file(tmp_path)
                if doc_hash is None:
                    return False
                self.document_hashes[name] = doc_hash

        documents = [
            self._build_document(doc, with_external_refs=True) for _, doc in self._documents()
        ]
        out_path = os.path.join(output_dir, f"{_SINGLE_FILE_BASENAME}.{ext}")

        if self.output_format == "tag-value":
            ok = self._write_tagvalue_bundle(documents, out_path)
        else:
            ok = self._write_json_bundle(documents, out_path)

        if ok:
            _logger.info("SPDX %s bundle written to %s", self.spdx_version, out_path)
        return ok

    # ------------------------------------------------------------------ #
    # Writing helpers
    # ------------------------------------------------------------------ #

    def _write_document(self, document, path):
        """Write a single Document to ``path`` (format inferred from extension)."""
        try:
            write_file(document, path, validate=False)
            return True
        except Exception:
            _logger.exception("Unable to write SPDX document to %s", path)
            return False

    def _hash_file(self, path):
        hashes = getHashes(path)
        if not hashes:
            _logger.error("Unable to calculate hash values for %s", path)
            return None
        return hashes[0]

    def _write_tagvalue_bundle(self, documents, out_path):
        """Concatenate tag-value renderings of all documents into one file."""
        try:
            sections = []
            with tempfile.TemporaryDirectory() as tmp:
                for index, document in enumerate(documents):
                    tmp_path = os.path.join(tmp, f"doc{index}.spdx")
                    write_file(document, tmp_path, validate=False)
                    with open(tmp_path) as f:
                        sections.append(f.read())
            with open(out_path, "w") as f:
                f.write("\n".join(sections))
            return True
        except Exception:
            _logger.exception("Unable to write tag-value bundle to %s", out_path)
            return False

    def _write_json_bundle(self, documents, out_path):
        """Bundle all documents into a single JSON container file."""
        from spdx_tools.spdx.jsonschema.document_converter import DocumentConverter

        try:
            converter = DocumentConverter()
            bundle = {"documents": [converter.convert(document) for document in documents]}
            with open(out_path, "w") as f:
                json.dump(bundle, f, indent=2)
            return True
        except Exception:
            _logger.exception("Unable to write JSON bundle to %s", out_path)
            return False

    # ------------------------------------------------------------------ #
    # SPDX ID generation
    # ------------------------------------------------------------------ #

    def _generate_ids(self):
        """Generate SPDX IDs for all components and files."""
        for component in self.sbom_graph.components.values():
            self.component_ids[component.name] = f"SPDXRef-{normalize_spdx_name(component.name)}"

        # Generate file IDs with a global collision check: keep incrementing the
        # suffix until an unused ID is found, regardless of processing order.
        used_ids = set(self.component_ids.values())
        for file_path in self.sbom_graph.files:
            safe_name = normalize_spdx_name(os.path.basename(file_path))
            spdx_id = f"SPDXRef-File-{safe_name}"

            count = 1
            while spdx_id in used_ids:
                count += 1
                spdx_id = f"SPDXRef-File-{safe_name}-{count}"

            used_ids.add(spdx_id)
            self.file_ids[file_path] = spdx_id

        for doc_name in self.sbom_graph.documents:
            self.document_refs[doc_name] = f"DocumentRef-{doc_name}"

    # ------------------------------------------------------------------ #
    # Document / element builders
    # ------------------------------------------------------------------ #

    def _build_document(self, doc: SBOMDocument, *, with_external_refs) -> Document:
        """Build an spdx-tools Document for a single SBOMDocument."""
        creation_info = self._build_creation_info(doc, with_external_refs=with_external_refs)

        packages = []
        files = []
        relationships = self._describes_relationships(doc)

        for component in doc.components.values():
            package = self._build_package(component)
            packages.append(package)
            relationships.extend(self._map_relationships(component.relationships, doc))

            for file_obj in sorted(component.files.values(), key=lambda x: x.relative_path):
                file_element = self._build_file(file_obj)
                files.append(file_element)
                # Package containment is modelled as a (comment-less) CONTAINS
                # relationship; the tag-value writer uses it to nest the file
                # under its package, and parsers reconstruct exactly one CONTAINS.
                relationships.append(
                    Relationship(package.spdx_id, RelationshipType.CONTAINS, file_element.spdx_id)
                )
                relationships.extend(self._map_relationships(file_obj.relationships, doc))

        return Document(
            creation_info=creation_info,
            packages=packages,
            files=files,
            relationships=relationships,
            extracted_licensing_info=self._extracted_licenses(doc),
        )

    def _build_creation_info(self, doc: SBOMDocument, *, with_external_refs) -> CreationInfo:
        namespace = doc.namespace or f"{self.sbom_graph.namespace_prefix}/{doc.name}"
        external_refs = self._build_external_document_refs(doc) if with_external_refs else []
        return CreationInfo(
            spdx_version=f"SPDX-{self.spdx_version}",
            spdx_id="SPDXRef-DOCUMENT",
            name=normalize_spdx_name(doc.title or doc.name),
            document_namespace=namespace,
            creators=[Actor(ActorType.TOOL, "Zephyr SPDX builder")],
            created=self._created,
            data_license="CC0-1.0",
            external_document_refs=external_refs,
        )

    def _build_external_document_refs(self, doc: SBOMDocument) -> list[ExternalDocumentRef]:
        refs = []
        for ext_doc in doc.external_documents.values():
            doc_hash = self.document_hashes.get(ext_doc.name)
            if not doc_hash:
                continue
            doc_ref_id = self.document_refs.get(ext_doc.name, f"DocumentRef-{ext_doc.name}")
            namespace = ext_doc.namespace or f"{self.sbom_graph.namespace_prefix}/{ext_doc.name}"
            refs.append(
                ExternalDocumentRef(
                    doc_ref_id, namespace, Checksum(ChecksumAlgorithm.SHA1, doc_hash)
                )
            )
        refs.sort(key=lambda ref: ref.document_ref_id)
        return refs

    def _build_package(self, component: SBOMComponent) -> Package:
        spdx_id = self._resolve_package_id(component)
        package_name, supplier, package_version = self._resolve_cpe_metadata(component)

        download_location = (
            generate_download_url(component.url, component.revision)
            if component.url
            else SpdxNoAssertion()
        )

        files_analyzed = bool(component.files)
        purpose = None
        if self.spdx_version >= SPDX_VERSION_2_3:
            purpose = mm.to_purpose(component.purpose)

        return Package(
            spdx_id=spdx_id,
            name=package_name,
            download_location=download_location,
            version=package_version or component.revision or None,
            supplier=mm.to_supplier(supplier),
            files_analyzed=files_analyzed,
            verification_code=mm.verification_code(component) if files_analyzed else None,
            license_concluded=mm.to_license(component.concluded_license),
            license_declared=mm.to_license(component.declared_license),
            license_info_from_files=(
                mm.license_info_from_files(component.license_info_from_files)
                if files_analyzed
                else None
            ),
            copyright_text=mm.to_copyright(component.copyright_text),
            comment=None if files_analyzed else "Utility target; no files",
            external_references=mm.to_external_refs(component.external_references),
            primary_package_purpose=purpose,
        )

    def _build_file(self, file_obj: SBOMFile) -> File:
        return File(
            name=f"./{file_obj.relative_path}",
            spdx_id=self.file_ids[file_obj.path],
            checksums=mm.to_checksums(file_obj.hashes),
            license_concluded=mm.to_license(file_obj.concluded_license),
            license_info_in_file=mm.license_info_in_file(file_obj.license_info_in_file),
            copyright_text=mm.to_copyright(file_obj.copyright_text),
        )

    def _describes_relationships(self, doc: SBOMDocument) -> list[Relationship]:
        """Build the document-level DESCRIBES relationships for primary subjects."""
        relationships = []
        for component_name in doc.described_components:
            if component_name not in doc.components:
                continue
            component_id = self.component_ids.get(component_name)
            if component_id:
                relationships.append(
                    Relationship("SPDXRef-DOCUMENT", RelationshipType.DESCRIBES, component_id)
                )
        return relationships

    def _map_relationships(self, sbom_relationships, current_doc: SBOMDocument):
        relationships = []
        for rel in sbom_relationships:
            relationships.extend(self._map_relationship(rel, current_doc))
        return relationships

    def _map_relationship(self, rel, current_doc: SBOMDocument) -> list[Relationship]:
        """Map one SBOMRelationship to spdx-tools Relationship(s)."""
        from_id = self._get_id_for_element(rel.from_element)
        if not from_id:
            return []

        try:
            rel_type = RelationshipType[rel.relationship_type.name]
        except KeyError:
            _logger.warning("Unknown relationship type: %s", rel.relationship_type)
            return []

        relationships = []
        for to_elem in rel.to_elements:
            to_id = self._get_id_for_element(to_elem)
            if not to_id:
                continue
            to_doc = self.sbom_graph.get_document_for_element(to_elem)
            if to_doc and to_doc.name != current_doc.name:
                doc_ref = self.document_refs.get(to_doc.name, "")
                if doc_ref:
                    to_id = f"{doc_ref}:{to_id}"
            relationships.append(Relationship(from_id, rel_type, to_id))
        return relationships

    def _extracted_licenses(self, doc: SBOMDocument) -> list[ExtractedLicensingInfo]:
        """Build ExtractedLicensingInfo entries for the document's custom licenses."""
        standard_licenses = get_standard_licenses()
        custom_licenses = set()
        for component in doc.components.values():
            for file_obj in component.files.values():
                for lic in file_obj.license_info_in_file:
                    if lic not in standard_licenses:
                        custom_licenses.add(lic)
        custom_licenses.update(doc.custom_license_ids)

        infos = []
        for lic in sorted(custom_licenses):
            # REUSE-IgnoreStart
            comment = (
                f"Corresponds to the license ID `{lic}` detected in an "
                "SPDX-License-Identifier: tag."
            )
            # REUSE-IgnoreEnd
            infos.append(
                ExtractedLicensingInfo(
                    license_id=lic,
                    extracted_text=lic,
                    license_name=lic,
                    comment=comment,
                )
            )
        return infos

    # ------------------------------------------------------------------ #
    # ID resolution helpers
    # ------------------------------------------------------------------ #

    def _resolve_package_id(self, component):
        """Get the SPDX ID for a component, generating one as a fallback if missing."""
        spdx_id = self.component_ids.get(component.name)
        if spdx_id:
            return spdx_id

        _logger.error(f"Component {component.name} not found in component_ids")
        spdx_id = f"SPDXRef-{normalize_spdx_name(component.name)}"
        self.component_ids[component.name] = spdx_id
        return spdx_id

    def _resolve_cpe_metadata(self, component):
        """Derive (name, supplier, version), filling gaps from a CPE 2.3 reference.

        Returns local copies so component.name is left untouched (it drives ID lookup).
        """
        package_name = component.name
        supplier = component.supplier
        package_version = component.version
        for ref in component.external_references:
            if ref.reference_type != ExternalReferenceType.CPE23:
                continue
            # metadata: [cpe, 2.3, a, arm, mbed_tls, 3.5.1, *:*:*:*:*:*]
            metadata = ref.locator.split(':', 6)
            if len(metadata) > 5:
                supplier = supplier or metadata[3]
                package_name = metadata[4]
                package_version = package_version or metadata[5]
        return package_name, supplier, package_version

    def _get_id_for_element(self, element: SBOMElement) -> str:
        """Get the SPDX ID generated for an SBOM element."""
        if isinstance(element, SBOMComponent):
            return self.component_ids.get(element.name, "")
        if isinstance(element, SBOMFile):
            return self.file_ids.get(element.path, "")
        return ""
