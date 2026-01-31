# Copyright (c) 2020-2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import os
import re
from datetime import datetime

from west import log

from zspdx.model import ComponentPurpose, SBomDocument, SBOMComponent, SBOMFile
from zspdx.serializers.helpers import (
    CPE23TYPE_REGEX,
    PURL_REGEX,
    generate_download_url,
    get_standard_licenses,
    normalize_spdx_name,
)
from zspdx.util import getHashes
from zspdx.version import SPDX_VERSION_2_3


class SPDX2Serializer:
    """Serializer that converts SBOMData to SPDX 2.x tag-value format."""

    def __init__(self, sbom_data, spdx_version=SPDX_VERSION_2_3):
        self.sbom_data = sbom_data
        self.spdx_version = spdx_version

        # Track generated IDs
        self.component_ids = {}  # component_name -> SPDX ID
        self.file_ids = {}  # file_path -> SPDX ID
        self.document_refs = {}  # document_name -> DocumentRef ID

        # Build tool packages (created for build document)
        self.build_tool_ids = {}  # tool_name -> SPDX ID
        self.build_info = sbom_data.metadata.get('build_info', {}) if sbom_data.metadata else {}

    def serialize(self, output_dir):
        """Serialize SBOMData to SPDX 2.x format files."""
        # Generate IDs for all components and files (including build tools)
        self._generate_ids()
        # Generate IDs for build tools
        self._generate_build_tool_ids()

        # First pass: write all documents to calculate hashes
        written_docs = {}
        for doc in self.sbom_data.documents.values():
            if not doc.components:
                continue

            output_path = os.path.join(output_dir, f"{doc.name}.spdx")
            if self._write_document_first_pass(doc, output_path):
                written_docs[doc.name] = output_path
            else:
                log.err(f"Failed to write document {doc.name}")
                return False

        # Second pass: rewrite documents with external document references
        for doc in self.sbom_data.documents.values():
            if not doc.components or doc.name not in written_docs:
                continue

            output_path = written_docs[doc.name]
            if not self._write_document_second_pass(doc, output_path):
                log.err(f"Failed to rewrite document {doc.name} with external references")
                return False

        return True

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

    def _write_document_first_pass(self, doc: SBomDocument, output_path):
        """Write a single SPDX 2.x document (first pass, without external refs)."""
        try:
            log.inf(f"Writing SPDX {self.spdx_version} document {doc.name} to {output_path}")
            with open(output_path, "w") as f:
                self._write_document_header(f, doc)
                # Skip external document refs in first pass
                self._write_document_relationships(f, doc)
                self._write_packages(f, doc)
                self._write_custom_licenses(f, doc)

            # Calculate document hash and store it in the document object
            hashes = getHashes(output_path)
            if not hashes:
                log.err(
                    f"Error: created document but unable to calculate hash values for {output_path}"
                )
                return False
            doc.doc_hash = hashes[0]

            return True
        except OSError as e:
            log.err(f"Error: Unable to write to {output_path}: {str(e)}")
            return False

    def _write_document_second_pass(self, doc: SBomDocument, output_path):
        """Rewrite document with external document references (second pass)."""
        try:
            # Write the updated document
            with open(output_path, "w") as f:
                # Write header
                self._write_document_header(f, doc)
                # Write external document refs (now we have all hashes)
                self._write_external_document_refs(f, doc)
                # Write the rest (relationships, packages, licenses)
                self._write_document_relationships(f, doc)
                self._write_packages(f, doc)
                self._write_custom_licenses(f, doc)

            # Recalculate hash after adding external refs
            hashes = getHashes(output_path)
            if not hashes:
                log.err(f"Error: unable to recalculate hash for {output_path}")
                return False
            doc.doc_hash = hashes[0]

            return True
        except OSError as e:
            log.err(f"Error: Unable to rewrite {output_path}: {str(e)}")
            return False

    def _write_document_header(self, f, doc: SBomDocument):
        """Write SPDX document header."""
        # Use document's namespace if set, otherwise generate from prefix
        namespace = doc.namespace or f"{self.sbom_data.namespace_prefix}/{doc.name}"
        normalized_name = normalize_spdx_name(doc.name)

        f.write(f"""SPDXVersion: SPDX-{self.spdx_version}
DataLicense: CC0-1.0
SPDXID: SPDXRef-DOCUMENT
DocumentName: {normalized_name}
DocumentNamespace: {namespace}
Creator: Tool: Zephyr SPDX builder
Created: {datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")}

""")

    def _write_external_document_refs(self, f, doc: SBomDocument):
        """Write external document references."""
        ext_refs = []
        # Use document's external_documents which were populated during walking
        for ext_doc in doc.external_documents.values():
            if ext_doc.doc_hash:  # Only include if hash has been calculated
                ext_refs.append(
                    (ext_doc, self.document_refs.get(ext_doc.name, f"DocumentRef-{ext_doc.name}"))
                )

        if ext_refs:
            ext_refs.sort(key=lambda x: x[1])  # Sort by DocumentRef ID
            for ext_doc, doc_ref_id in ext_refs:
                namespace = ext_doc.namespace or f"{self.sbom_data.namespace_prefix}/{ext_doc.name}"
                f.write(f"ExternalDocumentRef: {doc_ref_id} {namespace} SHA1: {ext_doc.doc_hash}\n")
            f.write("\n")

    def _write_document_relationships(self, f, doc: SBomDocument):
        """Write document-level relationships (DESCRIBES)."""
        # Include build tool packages in document relationships for build document
        if doc.name == "build":
            for tool_id in self.build_tool_ids.values():
                f.write(f"Relationship: SPDXRef-DOCUMENT DESCRIBES {tool_id}\n")

        for component in doc.components.values():
            component_id = self.component_ids[component.name]
            f.write(f"Relationship: SPDXRef-DOCUMENT DESCRIBES {component_id}\n")
        if doc.components or (doc.name == "build" and self.build_tool_ids):
            f.write("\n")

    def _write_packages(self, f, doc: SBomDocument):
        """Write all packages in this document."""
        # Write build tool packages first if this is the build document
        if doc.name == "build":
            self._write_build_tools(f)

        for component in doc.components.values():
            self._write_package(f, component, doc)

    def _write_package(self, f, component, doc: SBomDocument):
        """Write a single package."""
        # Get SPDX ID first, before any name modifications
        spdx_id = self.component_ids.get(component.name)
        if not spdx_id:
            log.err(f"Component {component.name} not found in component_ids")
            # Generate ID on the fly as fallback
            spdx_id = f"SPDXRef-{normalize_spdx_name(component.name)}"
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
                    package_version = (
                        metadata[5]
                        if len(metadata) > 5 and not package_version
                        else package_version
                    )

        normalized_name = normalize_spdx_name(package_name)

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
            download_url = generate_download_url(component.url, component.revision)
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
            f.write(
                f"FilesAnalyzed: true\nPackageVerificationCode: {component.verification_code}\n"
            )

            # Add build info comment for build target packages
            if doc.name == "build" and self.build_info:
                build_comment = self._format_build_info_comment()
                if build_comment:
                    f.write(f"PackageComment: {build_comment}\n")
            f.write("\n")
        else:
            comment = "Utility target; no files"
            # Add build info comment for build tool packages
            if component.name.startswith("build-tool-") and self.build_info:
                build_comment = self._format_build_info_comment()
                if build_comment:
                    comment = build_comment
            f.write(f"FilesAnalyzed: false\nPackageComment: {comment}\n\n")

        # Package relationships
        for rel in component.relationships:
            self._write_relationship(f, rel, component.name, "component", doc)

        # Add build tool relationships for build target packages
        if doc.name == "build" and len(component.files) > 0:
            self._write_build_tool_relationships(f, component)

        # Package files
        if len(component.files) > 0:
            files_list = list(component.files.values())
            files_list.sort(key=lambda x: x.relative_path)
            for file_obj in files_list:
                self._write_file(f, file_obj, doc)

    def _write_file(self, f, file_obj, doc: SBomDocument):
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
            self._write_relationship(f, rel, file_obj.path, "file", doc)

    def _write_relationship(self, f, rel, from_identifier, from_type, current_doc: SBomDocument):
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
                to_doc_name = self._get_document_name_for_component(to_elem.name)
                if to_doc_name and to_doc_name != current_doc.name:
                    doc_ref = self.document_refs.get(to_doc_name, "")
            elif isinstance(to_elem, SBOMFile):
                to_id = self.file_ids.get(to_elem.path, "")
                # Check if it's in a different document
                if to_elem.component:
                    to_doc_name = self._get_document_name_for_component(to_elem.component.name)
                    if to_doc_name and to_doc_name != current_doc.name:
                        doc_ref = self.document_refs.get(to_doc_name, "")

            if to_id:
                if doc_ref:
                    to_id = f"{doc_ref}:{to_id}"
                f.write(f"Relationship: {from_id} {rel.relationship_type} {to_id}\n")

    def _get_document_name_for_component(self, component_name: str) -> str:
        """Get document name for a component."""
        for doc in self.sbom_data.documents.values():
            if component_name in doc.components:
                return doc.name
        return None

    def _write_custom_licenses(self, f, doc: SBomDocument):
        """Write custom license declarations."""
        # Get custom licenses from components in this document
        custom_licenses = set()
        standard_licenses = get_standard_licenses()
        for component in doc.components.values():
            for file_obj in component.files.values():
                for lic in file_obj.license_info_in_file:
                    if lic not in standard_licenses:
                        custom_licenses.add(lic)

        # Also include any custom licenses stored in the document
        custom_licenses.update(doc.custom_license_ids)

        if custom_licenses:
            for lic in sorted(custom_licenses):
                f.write(f"""LicenseID: {lic}
ExtractedText: {lic}
LicenseName: {lic}
LicenseComment: Corresponds to the license ID `{lic}` detected in an SPDX-License-Identifier: tag.
""")

    def _purpose_to_spdx_string(self, purpose):
        """Convert ComponentPurpose enum to SPDX 2.x string."""
        purpose_map = {
            ComponentPurpose.APPLICATION: "APPLICATION",
            ComponentPurpose.LIBRARY: "LIBRARY",
            ComponentPurpose.SOURCE: "SOURCE",
            ComponentPurpose.FILE: "FILE",
        }
        return purpose_map.get(purpose, "")

    def _generate_build_tool_ids(self):
        """Generate SPDX IDs for build tool packages."""
        if not self.build_info:
            return

        # CMake
        if self.build_info.get("cmake_version") or self.build_info.get("cmake_generator"):
            self.build_tool_ids["cmake"] = "SPDXRef-build-tool-cmake"
            self.component_ids["build-tool-cmake"] = "SPDXRef-build-tool-cmake"

        # C compiler
        if self.build_info.get("cmake_compiler"):
            self.build_tool_ids["c-compiler"] = "SPDXRef-build-tool-c-compiler"
            self.component_ids["build-tool-c-compiler"] = "SPDXRef-build-tool-c-compiler"

        # C++ compiler
        cxx_compiler_path = self.build_info.get("cmake_cxx_compiler", "")
        compiler_path = self.build_info.get("cmake_compiler", "")
        if cxx_compiler_path and cxx_compiler_path != compiler_path:
            self.build_tool_ids["cxx-compiler"] = "SPDXRef-build-tool-cxx-compiler"
            self.component_ids["build-tool-cxx-compiler"] = "SPDXRef-build-tool-cxx-compiler"

    def _write_build_tools(self, f):
        """Write build tool packages for the build document."""
        if not self.build_info:
            return

        # Create CMake tool package
        if self.build_info.get("cmake_version") or self.build_info.get("cmake_generator"):
            cmake_id = "SPDXRef-build-tool-cmake"
            cmake_name = "CMake"
            cmake_version = self.build_info.get("cmake_version", "")
            cmake_generator = self.build_info.get("cmake_generator", "")

            f.write(f"""##### Package: CMake

PackageName: {cmake_name}
SPDXID: {cmake_id}
PackageLicenseConcluded: NOASSERTION
PackageLicenseDeclared: NOASSERTION
PackageCopyrightText: NOASSERTION
""")

            if cmake_version:
                f.write(f"PackageVersion: {cmake_version}\n")
            f.write("PackageDownloadLocation: https://cmake.org/\n")

            if cmake_generator:
                f.write(
                    f"FilesAnalyzed: false\nPackageComment: <text>Build tool: CMake {cmake_version or ''} with generator {cmake_generator}</text>\n\n"
                )
            else:
                f.write(
                    f"FilesAnalyzed: false\nPackageComment: <text>Build tool: CMake {cmake_version or ''}</text>\n\n"
                )

        # Create C compiler tool package
        compiler_path = self.build_info.get("cmake_compiler", "")
        if compiler_path:
            compiler_id = "SPDXRef-build-tool-c-compiler"
            compiler_name = os.path.basename(compiler_path)
            system_processor = self.build_info.get("cmake_system_processor", "")

            f.write(f"""##### Package: C-Compiler

PackageName: {compiler_name}
SPDXID: {compiler_id}
PackageLicenseConcluded: NOASSERTION
PackageLicenseDeclared: NOASSERTION
PackageCopyrightText: NOASSERTION
PackageDownloadLocation: NOASSERTION
""")

            comment_parts = [f"Build tool: C compiler ({compiler_name})"]
            if compiler_path:
                comment_parts.append(f"Path: {compiler_path}")
            if system_processor:
                comment_parts.append(f"Target architecture: {system_processor}")

            f.write(
                f"FilesAnalyzed: false\nPackageComment: <text>{' | '.join(comment_parts)}</text>\n\n"
            )

        # Create C++ compiler tool package
        cxx_compiler_path = self.build_info.get("cmake_cxx_compiler", "")
        if cxx_compiler_path and cxx_compiler_path != compiler_path:
            cxx_compiler_id = "SPDXRef-build-tool-cxx-compiler"
            cxx_compiler_name = os.path.basename(cxx_compiler_path)

            f.write(f"""##### Package: CXX-Compiler

PackageName: {cxx_compiler_name}
SPDXID: {cxx_compiler_id}
PackageLicenseConcluded: NOASSERTION
PackageLicenseDeclared: NOASSERTION
PackageCopyrightText: NOASSERTION
PackageDownloadLocation: NOASSERTION
""")

            comment_parts = [f"Build tool: C++ compiler ({cxx_compiler_name})"]
            if cxx_compiler_path:
                comment_parts.append(f"Path: {cxx_compiler_path}")

            f.write(
                f"FilesAnalyzed: false\nPackageComment: <text>{' | '.join(comment_parts)}</text>\n\n"
            )

    def _write_build_tool_relationships(self, f, component):
        """Write relationships linking build artifacts to build tools."""
        if not self.build_tool_ids:
            return

        component_id = self.component_ids.get(component.name)
        if not component_id:
            return

        # Link to CMake if available
        if "cmake" in self.build_tool_ids:
            cmake_id = self.build_tool_ids["cmake"]
            f.write(f"Relationship: {component_id} GENERATED_FROM {cmake_id}\n")

        # Link to C compiler if available
        if "c-compiler" in self.build_tool_ids:
            compiler_id = self.build_tool_ids["c-compiler"]
            f.write(f"Relationship: {component_id} GENERATED_FROM {compiler_id}\n")

        # Link to C++ compiler if available
        if "cxx-compiler" in self.build_tool_ids:
            cxx_compiler_id = self.build_tool_ids["cxx-compiler"]
            f.write(f"Relationship: {component_id} GENERATED_FROM {cxx_compiler_id}\n")

    def _format_build_info_comment(self):
        """Format build information as a PackageComment."""
        if not self.build_info:
            return None

        parts = []

        if self.build_info.get("cmake_version"):
            parts.append(f"CMake {self.build_info['cmake_version']}")
        if self.build_info.get("cmake_generator"):
            parts.append(f"generator: {self.build_info['cmake_generator']}")
        if self.build_info.get("cmake_compiler"):
            compiler_name = os.path.basename(self.build_info['cmake_compiler'])
            parts.append(f"C compiler: {compiler_name}")
        if self.build_info.get("cmake_cxx_compiler"):
            cxx_compiler_name = os.path.basename(self.build_info['cmake_cxx_compiler'])
            parts.append(f"C++ compiler: {cxx_compiler_name}")
        if self.build_info.get("cmake_system_processor"):
            parts.append(f"target: {self.build_info['cmake_system_processor']}")

        if parts:
            return f"<text>Built with: {', '.join(parts)}</text>"
        return None
