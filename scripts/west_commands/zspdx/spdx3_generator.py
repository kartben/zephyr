# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import json
import os
import re
from datetime import datetime, timezone

from spdx_python_model import v3_0_1 as spdx
from west import log

# Regex patterns for external reference validation
CPE23TYPE_REGEX = (
    r'^cpe:2\.3:[aho\*\-](:(((\?*|\*?)([a-zA-Z0-9\-\._]|(\\[\\\*\?!"#$$%&\'\(\)\+,\/:;<=>@\[\]\^'
    r"`\{\|}~]))+(\?*|\*?))|[\*\-])){5}(:(([a-zA-Z]{2,3}(-([a-zA-Z]{2}|[0-9]{3}))?)|[\*\-]))(:(((\?*"
    r'|\*?)([a-zA-Z0-9\-\._]|(\\[\\\*\?!"#$$%&\'\(\)\+,\/:;<=>@\[\]\^`\{\|}~]))+(\?*|\*?))|[\*\-])){4}$'
)
PURL_REGEX = r"^pkg:.+(\/.+)?\/.+(@.+)?(\?.+)?(#.+)?$"

# Mapping from SPDX 2.3 relationship types to SPDX 3.0 relationship types
SPDX_2_3_TO_3_0_RELATIONSHIP_MAPPING = {
    "AMENDS": "amendedBy",
    "ANCESTOR_OF": "ancestorOf",
    "BUILD_DEPENDENCY_OF": "dependsOn",  # Closest match
    "BUILD_TOOL_OF": "usesTool",  # Closest match
    "CONTAINED_BY": "contains",  # Reverse direction
    "CONTAINS": "contains",
    "COPY_OF": "copiedTo",  # Closest match
    "DATA_FILE_OF": "hasDataFile",  # Closest match
    "DEPENDENCY_MANIFEST_OF": "hasDependencyManifest",  # Closest match
    "DEPENDENCY_OF": "dependsOn",
    "DEPENDS_ON": "dependsOn",
    "DESCENDANT_OF": "descendantOf",
    "DESCRIBED_BY": "describes",  # Reverse direction
    "DESCRIBES": "describes",
    "DEV_DEPENDENCY_OF": "dependsOn",  # Treat as regular dependency
    "DEV_TOOL_OF": "usesTool",  # Closest match
    "DISTRIBUTION_ARTIFACT": "hasDistributionArtifact",  # Closest match
    "DOCUMENTATION_OF": "hasDocumentation",  # Closest match
    "DYNAMIC_LINK": "hasDynamicLink",
    "EXAMPLE_OF": "hasExample",  # Closest match
    "EXPANDED_FROM_ARCHIVE": "expandsTo",  # Closest match
    "FILE_ADDED": "hasAddedFile",
    "FILE_DELETED": "hasDeletedFile",
    "FILE_MODIFIED": "modifiedBy",  # Closest match
    "GENERATED_FROM": "generates",  # Reverse direction
    "GENERATES": "generates",
    "HAS_PREREQUISITE": "hasPrerequisite",
    "METAFILE_OF": "hasMetadata",  # Closest match
    "OPTIONAL_COMPONENT_OF": "hasOptionalComponent",  # Closest match
    "OPTIONAL_DEPENDENCY_OF": "hasOptionalDependency",  # Closest match
    "OTHER": "other",
    "PACKAGE_OF": "packagedBy",  # Closest match
    "PATCH_APPLIED": "patchedBy",  # Closest match
    "PATCH_FOR": "patchedBy",
    "PREREQUISITE_FOR": "hasPrerequisite",  # Reverse direction
    "PROVIDED_DEPENDENCY_OF": "hasProvidedDependency",  # Closest match
    "REQUIREMENT_DESCRIPTION_FOR": "hasRequirement",  # Closest match
    "RUNTIME_DEPENDENCY_OF": "dependsOn",  # Treat as regular dependency
    "SPECIFICATION_FOR": "hasSpecification",  # Closest match
    "STATIC_LINK": "hasStaticLink",
    "TEST_CASE_OF": "hasTestCase",  # Closest match
    "TEST_DEPENDENCY_OF": "dependsOn",  # Treat as regular dependency
    "TEST_OF": "hasTest",  # Closest match
    "TEST_TOOL_OF": "usesTool",  # Closest match
    "VARIANT_OF": "hasVariant",  # Closest match
}


def _map_spdx2_to_spdx3_relationship_type(spdx2_type: str) -> str:
    """Map SPDX 2.3 relationship type to SPDX 3.0 relationship type URI"""
    mapped_type = SPDX_2_3_TO_3_0_RELATIONSHIP_MAPPING.get(spdx2_type, "other")
    return f"https://spdx.org/rdf/3.0.1/terms/Core/RelationshipType/{mapped_type}"


class SPDX3Config:
    """Configuration for SPDX 3.0 generation using spdx-python-model"""

    def __init__(self):
        # prefix for Document namespaces; should not end with "/"
        self.namespacePrefix = ""

        # location of build directory
        self.buildDir = ""

        # location of SPDX document output directory
        self.spdxDir = ""

        # should also analyze for included header files?
        self.analyzeIncludes = False

        # should also add an SPDX document for the SDK?
        self.includeSDK = False


class SPDX3Generator:
    """Generator for SPDX 3.0 documents using spdx-python-model"""

    def __init__(self, config: SPDX3Config):
        self.config = config
        self.elements = []  # Store all elements
        self.processed_file_ids = set()  # Track processed file IDs to avoid duplicates
        self.global_spdx_id_to_uri = {}  # Global mapping for cross-document ID resolution

        # Create shared tool and creation info once to avoid duplicates
        self.tool_spdx_id = f"{self.config.namespacePrefix}/agents/west-spdx-tool"
        self.creation_info_id = f"{self.config.namespacePrefix}/creationinfo"
        self.tool = None
        self.creation_info = None

    def _initialize_shared_objects(self):
        """Initialize shared tool and creation info objects"""
        if self.tool is None:
            # Create the tool as Agent instead of Tool for SHACL compliance
            self.tool = spdx.Agent()
            self.tool._obj_data['@id'] = self.tool_spdx_id
            self.tool.name = "West SPDX Tool"
            # Set creation info for the tool itself
            self.tool._obj_data['https://spdx.org/rdf/3.0.1/terms/Core/creationInfo'] = (
                self.creation_info_id
            )

        if self.creation_info is None:
            # Create creation info once with proper IRI
            self.creation_info = spdx.CreationInfo()
            self.creation_info._obj_data['@id'] = self.creation_info_id
            self.creation_info.created = datetime.now(timezone.utc)
            self.creation_info.createdBy.append(self.tool_spdx_id)
            self.creation_info.specVersion = "3.0.1"

    def _create_document_with_elements(self, name: str, doc_id: str, elements: list) -> object:
        """Create an SPDX 3.0 document with proper element references"""
        # Initialize shared objects and add tool and creation info to elements if not already added
        self._initialize_shared_objects()
        if self.creation_info not in self.elements:
            self.elements.append(self.creation_info)
        if self.tool not in self.elements:
            self.elements.append(self.tool)

        document = spdx.SpdxDocument()
        document._obj_data['@id'] = doc_id
        document.name = name
        # Reference creation info by ID instead of embedding
        document._obj_data['https://spdx.org/rdf/3.0.1/terms/Core/creationInfo'] = (
            self.creation_info_id
        )

        # Set document-level properties - create proper license object
        license_expr = spdx.simplelicensing_LicenseExpression()
        license_expr._obj_data['@id'] = "https://spdx.org/licenses/CC0-1.0"
        license_expr.simplelicensing_licenseExpression = "CC0-1.0"
        # Reference creation info by ID
        license_expr._obj_data['https://spdx.org/rdf/3.0.1/terms/Core/creationInfo'] = (
            self.creation_info_id
        )
        document.dataLicense = license_expr
        document.profileConformance.append(
            "https://spdx.org/rdf/3.0.1/terms/Core/ProfileIdentifierType/core"
        )

        # Set element list - these should be ID references
        # Only include actual Elements, not CreationInfo which is metadata
        element_ids = [elem._obj_data['@id'] for elem in elements if '@id' in elem._obj_data]
        element_ids.append(self.tool_spdx_id)  # Include the tool (it's an Agent/Element)
        # Don't include creation info in elements list - it's metadata, not an Element
        for elem_id in element_ids:
            document.element.append(elem_id)

        # Root elements are typically the main packages
        root_element_ids = [
            elem._obj_data['@id']
            for elem in elements
            if hasattr(elem, '__class__') and elem.__class__.__name__ == 'software_Package'
        ]
        for root_id in root_element_ids:
            document.rootElement.append(root_id)

        self.elements.append(document)
        return document

    def _convert_zspdx_elements_to_spdx3(self, zspdx_doc):
        """Convert zspdx Document structure to SPDX 3.0 elements without creating SpdxDocument"""
        doc_name = zspdx_doc.cfg.name

        # Collect all elements for the document
        document_elements = []

        # Convert packages to Software elements
        for _, pkg in zspdx_doc.pkgs.items():
            software_element = self._convert_package_to_software(pkg)
            document_elements.append(software_element)
            self.elements.append(software_element)

            # Convert files to artifacts - check for duplicates
            for _, file_obj in pkg.files.items():
                artifact_id = f"{self.config.namespacePrefix}/files/{file_obj.spdxID}"

                # Only create file if we haven't seen this ID before
                if artifact_id not in self.processed_file_ids:
                    self.processed_file_ids.add(artifact_id)
                    artifact = self._convert_file_to_artifact(file_obj)
                    document_elements.append(artifact)
                    self.elements.append(artifact)

        # Process all relationships from the zspdx document
        # Document-level relationships (typically DESCRIBES relationships)
        if hasattr(zspdx_doc, 'relationships') and zspdx_doc.relationships:
            for zspdx_rel in zspdx_doc.relationships:
                spdx3_rel = self._convert_zspdx_relationship_to_spdx3(
                    zspdx_rel, zspdx_doc, doc_name
                )
                if spdx3_rel:
                    document_elements.append(spdx3_rel)
                    self.elements.append(spdx3_rel)

        # Package-level relationships (dependencies, etc.)
        for _, pkg in zspdx_doc.pkgs.items():
            if hasattr(pkg, 'rlns') and pkg.rlns:
                for zspdx_rel in pkg.rlns:
                    spdx3_rel = self._convert_zspdx_relationship_to_spdx3(
                        zspdx_rel, zspdx_doc, doc_name
                    )
                    if spdx3_rel:
                        document_elements.append(spdx3_rel)
                        self.elements.append(spdx3_rel)

            # File-level relationships
            for _, file_obj in pkg.files.items():
                if hasattr(file_obj, 'rlns') and file_obj.rlns:
                    for zspdx_rel in file_obj.rlns:
                        spdx3_rel = self._convert_zspdx_relationship_to_spdx3(
                            zspdx_rel, zspdx_doc, doc_name
                        )
                        if spdx3_rel:
                            document_elements.append(spdx3_rel)
                            self.elements.append(spdx3_rel)

        return document_elements

    def _convert_zspdx_relationship_to_spdx3(self, zspdx_rel, zspdx_doc, doc_name):
        """Convert a zspdx Relationship to SPDX 3.0 Relationship element"""
        # Use shared creation info
        self._initialize_shared_objects()

        # Create SPDX 3.0 Relationship
        spdx3_rel = spdx.Relationship()

        # Generate unique ID for this relationship
        rel_id = (
            f"{self.config.namespacePrefix}/relationships/"
            f"{zspdx_rel.rlnType.lower()}-{hash(zspdx_rel.refA + zspdx_rel.refB)}-{doc_name}"
        )
        spdx3_rel._obj_data['@id'] = rel_id

        # Reference creation info by ID
        spdx3_rel._obj_data['https://spdx.org/rdf/3.0.1/terms/Core/creationInfo'] = (
            self.creation_info_id
        )

        # Convert SPDX IDs to full URIs
        from_uri = self._convert_spdx_id_to_uri(zspdx_rel.refA, zspdx_doc)
        to_uri = self._convert_spdx_id_to_uri(zspdx_rel.refB, zspdx_doc)

        if not from_uri or not to_uri:
            log.wrn(
                f"Could not resolve SPDX IDs for relationship: "
                f"{zspdx_rel.refA} {zspdx_rel.rlnType} {zspdx_rel.refB}"
            )
            return None

        # Set relationship properties
        spdx3_rel._obj_data['https://spdx.org/rdf/3.0.1/terms/Core/from'] = from_uri
        spdx3_rel.to.append(to_uri)

        # Map relationship type from SPDX 2.3 to SPDX 3.0
        spdx3_rel.relationshipType = _map_spdx2_to_spdx3_relationship_type(zspdx_rel.rlnType)

        return spdx3_rel

    def _convert_spdx_id_to_uri(self, spdx_id, zspdx_doc):
        """Convert an SPDX ID (like SPDXRef-app-sources) to a full URI"""
        if not spdx_id:
            return None

        # Handle external document references (format: DocumentRef-xxx:SPDXRef-yyy)
        if ":" in spdx_id:
            doc_ref, local_ref = spdx_id.split(":", 1)
            # Check global mapping first
            if local_ref in self.global_spdx_id_to_uri:
                return self.global_spdx_id_to_uri[local_ref]
            # For external references we can't resolve, use a placeholder approach
            log.wrn(f"External document reference not fully supported: {spdx_id}")
            return f"{self.config.namespacePrefix}/external/{doc_ref}/{local_ref}"

        # Check global mapping first (this handles cross-document references)
        if spdx_id in self.global_spdx_id_to_uri:
            return self.global_spdx_id_to_uri[spdx_id]

        # Fallback: local search for backward compatibility
        # Handle special case of document reference
        if spdx_id == "SPDXRef-DOCUMENT":
            return f"https://spdx.org/documents/{zspdx_doc.cfg.name}"

        # For local references, look up in packages and files
        if spdx_id.startswith("SPDXRef-"):
            # Check if it's a package
            for _, pkg in zspdx_doc.pkgs.items():
                if pkg.cfg.spdxID == spdx_id:
                    return f"{self.config.namespacePrefix}/packages/{spdx_id}"

                # Check if it's a file in this package
                for _, file_obj in pkg.files.items():
                    if file_obj.spdxID == spdx_id:
                        return f"{self.config.namespacePrefix}/files/{spdx_id}"

        # If we can't resolve it, create a generic URI
        log.wrn(f"Could not resolve SPDX ID to specific element: {spdx_id}")
        return f"{self.config.namespacePrefix}/elements/{spdx_id}"

    def _convert_zspdx_to_spdx3(self, zspdx_doc):
        """Convert zspdx Document structure to SPDX 3.0 elements"""
        # Create main document
        doc_name = zspdx_doc.cfg.name
        doc_id = f"https://spdx.org/documents/{doc_name}"

        # Get the elements
        document_elements = self._convert_zspdx_elements_to_spdx3(zspdx_doc)

        # Now create the document with proper element lists
        spdx_doc = self._create_document_with_elements(doc_name, doc_id, document_elements)

        # Create relationships from document to root elements
        root_element_ids = spdx_doc.rootElement or []
        for root_id in root_element_ids:
            describes_rel = spdx.Relationship()
            describes_rel._obj_data['@id'] = (
                f"{doc_id}/relationships/describes-{root_id.split('/')[-1]}"
            )
            # Reference creation info by ID instead of embedding
            describes_rel._obj_data['https://spdx.org/rdf/3.0.1/terms/Core/creationInfo'] = (
                self.creation_info_id
            )
            describes_rel._obj_data['https://spdx.org/rdf/3.0.1/terms/Core/from'] = (
                spdx_doc._obj_data['@id']
            )
            describes_rel.relationshipType = (
                "https://spdx.org/rdf/3.0.1/terms/Core/RelationshipType/describes"
            )
            describes_rel.to.append(root_id)

            self.elements.append(describes_rel)

        return spdx_doc

    def _convert_package_to_software(self, zspdx_pkg):
        """Convert a zspdx Package to SPDX 3.0 Package element"""
        software_id = f"{self.config.namespacePrefix}/packages/{zspdx_pkg.cfg.spdxID}"

        # Use shared creation info
        self._initialize_shared_objects()

        # Create SPDX 3.0 Package element
        software = spdx.software_Package()
        software._obj_data['@id'] = software_id
        software.name = zspdx_pkg.cfg.name
        # Reference creation info by ID instead of embedding
        software._obj_data['https://spdx.org/rdf/3.0.1/terms/Core/creationInfo'] = (
            self.creation_info_id
        )

        # Set software purpose using the correct property name
        if "app" in zspdx_pkg.cfg.name:
            software.software_primaryPurpose = (
                "https://spdx.org/rdf/3.0.1/terms/Software/SoftwarePurpose/application"
            )
        elif "zephyr" in zspdx_pkg.cfg.name:
            software.software_primaryPurpose = (
                "https://spdx.org/rdf/3.0.1/terms/Software/SoftwarePurpose/framework"
            )
        else:
            software.software_primaryPurpose = (
                "https://spdx.org/rdf/3.0.1/terms/Software/SoftwarePurpose/library"
            )

        # Set other package fields using correct property names
        software.software_downloadLocation = getattr(zspdx_pkg.cfg, 'url', None) or "NOASSERTION"
        software.software_copyrightText = getattr(zspdx_pkg.cfg, 'copyrightText', "NOASSERTION")

        if hasattr(zspdx_pkg.cfg, 'version') and zspdx_pkg.cfg.version:
            software.software_packageVersion = zspdx_pkg.cfg.version

        # Handle external references (PURL and CPE)
        if hasattr(zspdx_pkg.cfg, 'externalReferences') and zspdx_pkg.cfg.externalReferences:
            for ref in zspdx_pkg.cfg.externalReferences:
                if re.fullmatch(CPE23TYPE_REGEX, ref):
                    # Create CPE external identifier
                    cpe_id = spdx.ExternalIdentifier()
                    cpe_id.externalIdentifierType = (
                        "https://spdx.org/rdf/3.0.1/terms/Core/ExternalIdentifierType/cpe23"
                    )
                    cpe_id.identifier = ref
                    software.externalIdentifier.append(cpe_id)
                elif re.fullmatch(PURL_REGEX, ref):
                    # For PURL, we can use the dedicated packageUrl field OR external identifier
                    # SPDX 3.0 prefers the dedicated packageUrl field
                    if (
                        not hasattr(software, 'software_packageUrl')
                        or not software.software_packageUrl
                    ):
                        software.software_packageUrl = ref
                    else:
                        # If packageUrl is already set, add as external identifier
                        purl_id = spdx.ExternalIdentifier()
                        purl_id.externalIdentifierType = "https://spdx.org/rdf/3.0.1/terms/Core/ExternalIdentifierType/packageUrl"
                        purl_id.identifier = ref
                        software.externalIdentifier.append(purl_id)
                else:
                    log.wrn(
                        f"Unknown external reference format ({ref}) for package"
                        f"{zspdx_pkg.cfg.name}"
                    )

        return software

    def _convert_file_to_artifact(self, zspdx_file):
        """Convert a zspdx File to SPDX 3.0 File"""
        artifact_id = f"{self.config.namespacePrefix}/files/{zspdx_file.spdxID}"

        # Use shared creation info
        self._initialize_shared_objects()

        # Create SPDX 3.0 File element
        artifact = spdx.software_File()
        artifact._obj_data['@id'] = artifact_id
        artifact.name = zspdx_file.relpath
        # Reference creation info by ID instead of embedding
        artifact._obj_data['https://spdx.org/rdf/3.0.1/terms/Core/creationInfo'] = (
            self.creation_info_id
        )
        artifact.software_primaryPurpose = (
            "https://spdx.org/rdf/3.0.1/terms/Software/SoftwarePurpose/source"
        )
        artifact.software_copyrightText = getattr(zspdx_file, 'copyrightText', "NOASSERTION")

        # Add hashes if available
        if hasattr(zspdx_file, 'sha1') and zspdx_file.sha1:
            hash_obj = spdx.Hash()
            hash_obj._obj_data['https://spdx.org/rdf/3.0.1/terms/Core/algorithm'] = (
                "https://spdx.org/rdf/3.0.1/terms/Core/HashAlgorithm/sha1"
            )
            hash_obj._obj_data['https://spdx.org/rdf/3.0.1/terms/Core/hashValue'] = zspdx_file.sha1
            artifact.verifiedUsing.append(hash_obj)

        return artifact

    def _serialize_to_json(self, output_file: str):
        """Serialize all elements to JSON format"""
        # Create a JSON-LD structure
        elements_data = []

        for element in self.elements:
            # Use the encode method from spdx-python-model with EncodeState
            encoder = spdx.JSONLDEncoder()
            state = spdx.EncodeState()  # Use proper EncodeState object
            element.encode(encoder, state)
            # The encoder.data should contain the serialized element
            if encoder.data:
                elements_data.append(encoder.data)

        # Create the JSON-LD document structure
        complete_dict = {
            "@context": "https://spdx.org/rdf/3.0.1/spdx-context.jsonld",
            "@graph": elements_data,
        }

        # Write the JSON-LD
        with open(output_file + ".jsonld", "w") as f:
            json.dump(complete_dict, f, indent=2)

        log.inf(f"Written SPDX 3.0 JSON-LD to {output_file}.jsonld")

        # Also create a plain JSON version by converting @type to type and @id to id
        def convert_at_symbols(obj):
            if isinstance(obj, dict):
                new_obj = {}
                for key, value in obj.items():
                    if key == "@type":
                        new_obj["type"] = convert_at_symbols(value)
                    elif key == "@id":
                        new_obj["id"] = convert_at_symbols(value)
                    elif key == "spdxId":  # Keep spdxId as is for compatibility
                        new_obj["spdxId"] = convert_at_symbols(value)
                    else:
                        new_obj[key] = convert_at_symbols(value)
                return new_obj
            elif isinstance(obj, list):
                return [convert_at_symbols(item) for item in obj]
            else:
                return obj

        plain_dict = convert_at_symbols(complete_dict)

        # Write the plain JSON
        with open(output_file + ".json", "w") as f:
            json.dump(plain_dict, f, indent=2)

        log.inf(f"Also created plain JSON version: {output_file}.json")

    def generate_spdx3_from_walker(self, walker) -> bool:
        """Generate SPDX 3.0 documents from an existing walker instance"""
        try:
            # Build a global mapping of SPDX IDs to URIs for cross-document resolution
            self.global_spdx_id_to_uri = {}

            # Collect all elements from different documents but create only one SpdxDocument
            all_document_elements = []
            documents_to_convert = []

            if self.config.includeSDK and hasattr(walker, 'docSDK'):
                documents_to_convert.append(('sdk', walker.docSDK))

            documents_to_convert.extend(
                [
                    ('app', walker.docApp),
                    ('zephyr', walker.docZephyr),
                    ('build', walker.docBuild),
                ]
            )

            # Always include modules dependencies document if it exists
            if hasattr(walker, 'docModulesExtRefs') and walker.docModulesExtRefs:
                documents_to_convert.append(('modules-deps', walker.docModulesExtRefs))

            # First pass: build global ID mapping
            for doc_name, zspdx_doc in documents_to_convert:
                self._build_global_id_mapping(zspdx_doc, doc_name)

            # Convert elements from each document but don't create separate SpdxDocuments
            for doc_name, zspdx_doc in documents_to_convert:
                log.inf(f"Converting {doc_name} elements to SPDX 3.0")
                elements = self._convert_zspdx_elements_to_spdx3(zspdx_doc)
                all_document_elements.extend(elements)

            # Create one unified SPDX document containing all elements
            unified_doc_id = "https://spdx.org/documents/zephyr-unified"
            self._create_document_with_elements(
                "Zephyr Unified SBOM", unified_doc_id, all_document_elements
            )

            # Write the combined payload
            output_file = os.path.join(self.config.spdxDir, "zephyr-spdx3")
            log.inf(f"Writing SPDX 3.0 document to {output_file}")
            self._serialize_to_json(output_file)

            return True

        except Exception as e:
            log.err(f"Failed to generate SPDX 3.0 document: {e}")
            return False

    def _build_global_id_mapping(self, zspdx_doc, doc_name):
        """Build global mapping of SPDX IDs to URIs for cross-document resolution"""
        # Map document itself
        self.global_spdx_id_to_uri["SPDXRef-DOCUMENT"] = (
            f"https://spdx.org/documents/{zspdx_doc.cfg.name}"
        )

        # Map packages
        for _, pkg in zspdx_doc.pkgs.items():
            if pkg.cfg.spdxID:
                self.global_spdx_id_to_uri[pkg.cfg.spdxID] = (
                    f"{self.config.namespacePrefix}/packages/{pkg.cfg.spdxID}"
                )

                # Map files in packages
                for _, file_obj in pkg.files.items():
                    if file_obj.spdxID:
                        self.global_spdx_id_to_uri[file_obj.spdxID] = (
                            f"{self.config.namespacePrefix}/files/{file_obj.spdxID}"
                        )
