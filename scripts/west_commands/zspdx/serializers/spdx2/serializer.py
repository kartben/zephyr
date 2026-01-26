# Copyright (c) 2020-2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import os
import re
from collections import defaultdict
from datetime import datetime

from west import log

from zspdx.model import ComponentPurpose, SBOMComponent, SBOMData, SBOMFile
from zspdx.util import getHashes
from zspdx.version import SPDX_VERSION_2_2, SPDX_VERSION_2_3

CPE23TYPE_REGEX = (
    r'^cpe:2\.3:[aho\*\-](:(((\?*|\*?)([a-zA-Z0-9\-\._]|(\\[\\\*\?!"#$$%&\'\(\)\+,\/:;<=>@\[\]\^'
    r"`\{\|}~]))+(\?*|\*?))|[\*\-])){5}(:(([a-zA-Z]{2,3}(-([a-zA-Z]{2}|[0-9]{3}))?)|[\*\-]))(:(((\?*"
    r'|\*?)([a-zA-Z0-9\-\._]|(\\[\\\*\?!"#$$%&\'\(\)\+,\/:;<=>@\[\]\^`\{\|}~]))+(\?*|\*?))|[\*\-])){4}$'
)
PURL_REGEX = r"^pkg:.+(\/.+)?\/.+(@.+)?(\?.+)?(#.+)?$"


def _normalize_spdx_name(name):
    """Replace '_' by '-' since it's not allowed in SPDX ID."""
    return name.replace("_", "-")


def _generate_download_url(url, revision):
    """Generate download URL with revision if available."""
    if not revision:
        return url
    return f'git+{url}@{revision}'


class SPDX2Serializer:
    """Serializer that converts SBOMData to SPDX 2.x tag-value format."""

    def __init__(self, sbom_data, spdx_version=SPDX_VERSION_2_3):
        self.sbom_data = sbom_data
        self.spdx_version = spdx_version

        # Track generated IDs
        self.component_ids = {}  # component_name -> SPDX ID
        self.file_ids = {}  # file_path -> SPDX ID
        self.document_refs = {}  # document_name -> DocumentRef ID
        self.document_hashes = {}  # document_name -> SHA1 hash

        # Group components into documents
        self.documents = self._group_components_into_documents()

    def _group_components_into_documents(self):
        """Group components into SPDX 2.x documents based on their names/purposes."""
        documents = defaultdict(list)

        for component in self.sbom_data.components.values():
            doc_name = self._get_document_name(component)
            documents[doc_name].append(component)

        return dict(documents)

    def _get_document_name(self, component):
        """Determine which document a component belongs to."""
        name = component.name

        if name == "app-sources":
            return "app"
        elif name == "sdk-sources":
            return "sdk"
        elif name == "zephyr-sources" or name.endswith("-sources"):
            # zephyr-sources and module sources (e.g., "module-name-sources")
            return "zephyr"
        elif name.endswith("-deps"):
            return "modules-deps"
        else:
            # Build targets go into build document
            return "build"

    def serialize(self, output_dir):
        """Serialize SBOMData to SPDX 2.x format files."""
        # Generate IDs for all components and files
        self._generate_ids()

        # First pass: write all documents to calculate hashes
        written_docs = {}
        for doc_name, components in self.documents.items():
            if not components:
                continue

            output_path = os.path.join(output_dir, f"{doc_name}.spdx")
            if self._write_document_first_pass(doc_name, components, output_path):
                written_docs[doc_name] = output_path
            else:
                log.err(f"Failed to write document {doc_name}")
                return False

        # Second pass: rewrite documents with external document references
        for doc_name, components in self.documents.items():
            if not components or doc_name not in written_docs:
                continue

            output_path = written_docs[doc_name]
            if not self._write_document_second_pass(doc_name, components, output_path):
                log.err(f"Failed to rewrite document {doc_name} with external references")
                return False

        return True

    def _generate_ids(self):
        """Generate SPDX IDs for all components and files."""
        # Generate component IDs
        for component in self.sbom_data.components.values():
            spdx_id = f"SPDXRef-{_normalize_spdx_name(component.name)}"
            self.component_ids[component.name] = spdx_id

        # Generate file IDs (tracking filename counts for uniqueness)
        filename_counts = {}
        for file_path, file_obj in self.sbom_data.files.items():
            filename_only = os.path.basename(file_path)
            safe_name = _normalize_spdx_name(filename_only)
            count = filename_counts.get(safe_name, 0) + 1
            filename_counts[safe_name] = count

            if count == 1:
                spdx_id = f"SPDXRef-File-{safe_name}"
            else:
                spdx_id = f"SPDXRef-File-{safe_name}-{count}"

            self.file_ids[file_path] = spdx_id

        # Generate document reference IDs
        for doc_name in self.documents.keys():
            self.document_refs[doc_name] = f"DocumentRef-{doc_name}"

    def _write_document_first_pass(self, doc_name, components, output_path):
        """Write a single SPDX 2.x document (first pass, without external refs)."""
        try:
            log.inf(f"Writing SPDX {self.spdx_version} document {doc_name} to {output_path}")
            with open(output_path, "w") as f:
                self._write_document_header(f, doc_name)
                # Skip external document refs in first pass
                self._write_document_relationships(f, doc_name, components)
                self._write_packages(f, components)
                self._write_custom_licenses(f, doc_name)

            # Calculate document hash
            hashes = getHashes(output_path)
            if not hashes:
                log.err(f"Error: created document but unable to calculate hash values for {output_path}")
                return False
            self.document_hashes[doc_name] = hashes[0]

            return True
        except OSError as e:
            log.err(f"Error: Unable to write to {output_path}: {str(e)}")
            return False

    def _write_document_second_pass(self, doc_name, components, output_path):
        """Rewrite document with external document references (second pass)."""
        try:
            # Read the first pass content
            with open(output_path, "r") as f:
                lines = f.readlines()

            # Find where to insert external document refs (after header, before relationships)
            # Write the updated document
            with open(output_path, "w") as f:
                # Write header
                self._write_document_header(f, doc_name)
                # Write external document refs (now we have all hashes)
                self._write_external_document_refs(f, doc_name)
                # Write the rest (relationships, packages, licenses)
                self._write_document_relationships(f, doc_name, components)
                self._write_packages(f, components)
                self._write_custom_licenses(f, doc_name)

            # Recalculate hash after adding external refs
            hashes = getHashes(output_path)
            if not hashes:
                log.err(f"Error: unable to recalculate hash for {output_path}")
                return False
            self.document_hashes[doc_name] = hashes[0]

            return True
        except OSError as e:
            log.err(f"Error: Unable to rewrite {output_path}: {str(e)}")
            return False

    def _write_document_header(self, f, doc_name):
        """Write SPDX document header."""
        namespace = f"{self.sbom_data.namespace_prefix}/{doc_name}"
        normalized_name = _normalize_spdx_name(doc_name)

        f.write(f"""SPDXVersion: SPDX-{self.spdx_version}
DataLicense: CC0-1.0
SPDXID: SPDXRef-DOCUMENT
DocumentName: {normalized_name}
DocumentNamespace: {namespace}
Creator: Tool: Zephyr SPDX builder
Created: {datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")}

""")

    def _write_external_document_refs(self, f, doc_name):
        """Write external document references."""
        ext_refs = []
        for other_doc_name in self.documents.keys():
            if other_doc_name != doc_name and other_doc_name in self.document_hashes:
                ext_refs.append((other_doc_name, self.document_refs[other_doc_name]))

        if ext_refs:
            ext_refs.sort(key=lambda x: x[1])  # Sort by DocumentRef ID
            for other_doc_name, doc_ref_id in ext_refs:
                namespace = f"{self.sbom_data.namespace_prefix}/{other_doc_name}"
                f.write(
                    f"ExternalDocumentRef: {doc_ref_id} {namespace} "
                    f"SHA1: {self.document_hashes[other_doc_name]}\n"
                )
            f.write("\n")

    def _write_document_relationships(self, f, doc_name, components):
        """Write document-level relationships (DESCRIBES)."""
        for component in components:
            component_id = self.component_ids[component.name]
            f.write(f"Relationship: SPDXRef-DOCUMENT DESCRIBES {component_id}\n")
        if components:
            f.write("\n")

    def _write_packages(self, f, components):
        """Write all packages in this document."""
        for component in components:
            self._write_package(f, component)

    def _write_package(self, f, component):
        """Write a single package."""
        # Get SPDX ID first, before any name modifications
        spdx_id = self.component_ids.get(component.name)
        if not spdx_id:
            log.err(f"Component {component.name} not found in component_ids")
            # Generate ID on the fly as fallback
            spdx_id = f"SPDXRef-{_normalize_spdx_name(component.name)}"
            self.component_ids[component.name] = spdx_id

        # Update component metadata based on CPE references
        # Use local variables to avoid modifying component.name (which affects ID lookup)
        package_name = component.name
        supplier = component.supplier
        package_version = component.version
        for ref in component.external_references:
            if re.fullmatch(CPE23TYPE_REGEX, ref):
                metadata = ref.split(':', 6)
                # metadata: [cpe,2.3,a,arm,mbed_tls,3.5.1,*:*:*:*:*:*]
                if len(metadata) > 5:
                    supplier = metadata[3] if not supplier else supplier
                    package_name = metadata[4] if len(metadata) > 4 else package_name
                    package_version = metadata[5] if len(metadata) > 5 and not package_version else package_version

        normalized_name = _normalize_spdx_name(package_name)

        f.write(f"""##### Package: {normalized_name}

PackageName: {package_name}
SPDXID: {spdx_id}
PackageLicenseConcluded: {component.concluded_license}
PackageLicenseDeclared: {component.declared_license}
PackageCopyrightText: {component.copyright_text}
""")

        # PrimaryPackagePurpose is only available in SPDX 2.3 and later
        if self.spdx_version >= SPDX_VERSION_2_3:
            purpose_str = self._purpose_to_spdx_string(component.purpose)
            if purpose_str:
                f.write(f"PrimaryPackagePurpose: {purpose_str}\n")

        # Download location
        if component.url:
            download_url = _generate_download_url(component.url, component.revision)
            f.write(f"PackageDownloadLocation: {download_url}\n")
        else:
            f.write("PackageDownloadLocation: NOASSERTION\n")

        # Version
        if package_version:
            f.write(f"PackageVersion: {package_version}\n")
        elif component.revision:
            f.write(f"PackageVersion: {component.revision}\n")

        # Supplier
        if supplier:
            f.write(f"PackageSupplier: Organization: {supplier}\n")

        # External references
        for ref in component.external_references:
            if re.fullmatch(CPE23TYPE_REGEX, ref):
                f.write(f"ExternalRef: SECURITY cpe23Type {ref}\n")
            elif re.fullmatch(PURL_REGEX, ref):
                f.write(f"ExternalRef: PACKAGE-MANAGER purl {ref}\n")
            else:
                log.wrn(f"Unknown external reference ({ref})")

        # Files analyzed and verification code
        if len(component.files) > 0:
            if component.license_info_from_files:
                for lic in component.license_info_from_files:
                    f.write(f"PackageLicenseInfoFromFiles: {lic}\n")
            else:
                f.write("PackageLicenseInfoFromFiles: NOASSERTION\n")
            f.write(f"FilesAnalyzed: true\nPackageVerificationCode: {component.verification_code}\n\n")
        else:
            f.write("FilesAnalyzed: false\nPackageComment: Utility target; no files\n\n")

        # Package relationships
        for rel in component.relationships:
            self._write_relationship(f, rel, component.name, "component")

        # Package files
        if len(component.files) > 0:
            files_list = list(component.files.values())
            files_list.sort(key=lambda x: x.relative_path)
            for file_obj in files_list:
                self._write_file(f, file_obj)

    def _write_file(self, f, file_obj):
        """Write a single file."""
        spdx_id = self.file_ids[file_obj.path]

        f.write(f"""FileName: ./{file_obj.relative_path}
SPDXID: {spdx_id}
FileChecksum: SHA1: {file_obj.hashes.get('SHA1', '')}
""")

        if 'SHA256' in file_obj.hashes and file_obj.hashes['SHA256']:
            f.write(f"FileChecksum: SHA256: {file_obj.hashes['SHA256']}\n")
        if 'MD5' in file_obj.hashes and file_obj.hashes['MD5']:
            f.write(f"FileChecksum: MD5: {file_obj.hashes['MD5']}\n")

        f.write(f"LicenseConcluded: {file_obj.concluded_license}\n")

        if not file_obj.license_info_in_file:
            f.write("LicenseInfoInFile: NONE\n")
        else:
            for lic in file_obj.license_info_in_file:
                f.write(f"LicenseInfoInFile: {lic}\n")

        f.write(f"FileCopyrightText: {file_obj.copyright_text}\n\n")

        # File relationships
        for rel in file_obj.relationships:
            self._write_relationship(f, rel, file_obj.path, "file")

    def _write_relationship(self, f, rel, from_identifier, from_type):
        """Write a relationship."""
        # Get from ID
        if from_type == "component":
            from_id = self.component_ids.get(from_identifier, "")
        else:  # file
            from_id = self.file_ids.get(from_identifier, "")

        if not from_id:
            return

        # Get to ID(s)
        to_elements = rel.to_elements if isinstance(rel.to_elements, list) else [rel.to_elements]
        for to_elem in to_elements:
            to_id = None
            doc_ref = None

            if isinstance(to_elem, SBOMComponent):
                to_id = self.component_ids.get(to_elem.name, "")
                # Check if it's in a different document
                to_doc = self._get_document_name(to_elem)
                if to_doc != self._get_document_name_from_identifier(from_identifier, from_type):
                    doc_ref = self.document_refs.get(to_doc, "")
            elif isinstance(to_elem, SBOMFile):
                to_id = self.file_ids.get(to_elem.path, "")
                # Check if it's in a different document
                to_doc = self._get_document_name(to_elem.component) if to_elem.component else None
                from_doc = self._get_document_name_from_identifier(from_identifier, from_type)
                if to_doc and to_doc != from_doc:
                    doc_ref = self.document_refs.get(to_doc, "")

            if to_id:
                if doc_ref:
                    to_id = f"{doc_ref}:{to_id}"
                f.write(f"Relationship: {from_id} {rel.relationship_type} {to_id}\n")

    def _get_document_name_from_identifier(self, identifier, identifier_type):
        """Get document name for an identifier."""
        if identifier_type == "component":
            component = self.sbom_data.get_component(identifier)
            if component:
                return self._get_document_name(component)
        else:  # file
            file_obj = self.sbom_data.get_file(identifier)
            if file_obj and file_obj.component:
                return self._get_document_name(file_obj.component)
        return None

    def _write_custom_licenses(self, f, doc_name):
        """Write custom license declarations."""
        # Get custom licenses from components in this document
        custom_licenses = set()
        for component in self.documents.get(doc_name, []):
            for file_obj in component.files.values():
                for lic in file_obj.license_info_in_file:
                    if lic not in self._get_standard_licenses():
                        custom_licenses.add(lic)

        if custom_licenses:
            for lic in sorted(custom_licenses):
                f.write(f"""LicenseID: {lic}
ExtractedText: {lic}
LicenseName: {lic}
LicenseComment: Corresponds to the license ID `{lic}` detected in an SPDX-License-Identifier: tag.
""")

    def _get_standard_licenses(self):
        """Get set of standard SPDX license IDs."""
        # Import here to avoid circular dependency
        from zspdx.licenses import LICENSES
        return LICENSES

    def _purpose_to_spdx_string(self, purpose):
        """Convert ComponentPurpose enum to SPDX 2.x string."""
        purpose_map = {
            ComponentPurpose.APPLICATION: "APPLICATION",
            ComponentPurpose.LIBRARY: "LIBRARY",
            ComponentPurpose.SOURCE: "SOURCE",
            ComponentPurpose.FILE: "FILE",
        }
        return purpose_map.get(purpose, "")
