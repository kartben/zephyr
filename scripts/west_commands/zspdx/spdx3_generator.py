# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import os
import json
from datetime import datetime, timezone

from west import log

try:
    from spdx_python_model import v3_0_1 as spdx

    SPDX_PYTHON_MODEL_AVAILABLE = True
except ImportError:
    SPDX_PYTHON_MODEL_AVAILABLE = False
    log.wrn("spdx-python-model not available. Install spdx-python-model to enable SPDX 3.0 support")


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
        if not SPDX_PYTHON_MODEL_AVAILABLE:
            raise RuntimeError("spdx-python-model not available. Install spdx-python-model")

        self.config = config
        self.elements = []  # Store all elements
        self.processed_file_ids = set()  # Track processed file IDs to avoid duplicates

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

    def _analyze_cmake_build(self):
        """Analyze the CMake build directory for files and packages"""
        # Import zspdx modules to reuse existing CMake analysis logic
        from zspdx.walker import WalkerConfig, Walker

        # Set up walker configuration to reuse existing logic
        walker_cfg = WalkerConfig()
        walker_cfg.namespacePrefix = self.config.namespacePrefix
        walker_cfg.buildDir = self.config.buildDir
        walker_cfg.analyzeIncludes = self.config.analyzeIncludes
        walker_cfg.includeSDK = self.config.includeSDK

        # Create walker and analyze build
        walker = Walker(walker_cfg)
        if not walker.makeDocuments():
            log.err("Failed to analyze CMake build directory")
            return None

        return walker

    def _convert_zspdx_elements_to_spdx3(self, zspdx_doc):
        """Convert zspdx Document structure to SPDX 3.0 elements without creating SpdxDocument"""
        doc_name = zspdx_doc.cfg.name

        # Collect all elements for the document
        document_elements = []

        # Convert packages to Software elements
        for pkg_id, pkg in zspdx_doc.pkgs.items():
            software_element = self._convert_package_to_software(pkg)
            document_elements.append(software_element)
            self.elements.append(software_element)

            # Convert files to artifacts - check for duplicates
            for file_id, file_obj in pkg.files.items():
                artifact_id = f"{self.config.namespacePrefix}/files/{file_obj.spdxID}"

                # Only create file if we haven't seen this ID before
                if artifact_id not in self.processed_file_ids:
                    self.processed_file_ids.add(artifact_id)
                    artifact = self._convert_file_to_artifact(file_obj)
                    document_elements.append(artifact)
                    self.elements.append(artifact)
                else:
                    # Find the existing artifact by ID
                    artifact = next(
                        (
                            elem
                            for elem in self.elements
                            if hasattr(elem, '_obj_data')
                            and elem._obj_data.get('@id') == artifact_id
                        ),
                        None,
                    )

                if artifact:
                    # Create relationship from software to artifact - use shared creation info
                    contains_rel = spdx.Relationship()
                    contains_rel._obj_data['@id'] = (
                        f"{self.config.namespacePrefix}/relationships/contains-{file_id}-{pkg_id}-{doc_name}"
                    )
                    # Reference creation info by ID instead of embedding
                    contains_rel._obj_data['https://spdx.org/rdf/3.0.1/terms/Core/creationInfo'] = (
                        self.creation_info_id
                    )
                    # Note: 'from' is a Python keyword, but spdx-python-model handles this
                    contains_rel._obj_data['https://spdx.org/rdf/3.0.1/terms/Core/from'] = (
                        software_element._obj_data['@id']
                    )
                    contains_rel.relationshipType = (
                        "https://spdx.org/rdf/3.0.1/terms/Core/RelationshipType/contains"
                    )
                    contains_rel.to.append(artifact._obj_data['@id'])

                    document_elements.append(contains_rel)
                    self.elements.append(contains_rel)

        return document_elements

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

    def generate_spdx3(self) -> bool:
        """Main entry point to generate SPDX 3.0 documents"""
        if not SPDX_PYTHON_MODEL_AVAILABLE:
            log.err("SPDX 3.0 generation requires spdx-python-model. Please install it.")
            return False

        try:
            # Analyze the build using existing zspdx logic
            walker = self._analyze_cmake_build()
            if not walker:
                return False

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

            # Convert elements from each document but don't create separate SpdxDocuments
            for doc_name, zspdx_doc in documents_to_convert:
                log.inf(f"Converting {doc_name} elements to SPDX 3.0")
                elements = self._convert_zspdx_elements_to_spdx3(zspdx_doc)
                all_document_elements.extend(elements)

            # Create one unified SPDX document containing all elements
            unified_doc_id = f"https://spdx.org/documents/zephyr-unified"
            unified_doc = self._create_document_with_elements(
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


def generate_spdx3_from_config(config: SPDX3Config) -> bool:
    """Generate SPDX 3.0 documents from configuration using spdx-python-model"""
    if not SPDX_PYTHON_MODEL_AVAILABLE:
        log.err("SPDX 3.0 generation requires spdx-python-model. Please install it.")
        return False

    generator = SPDX3Generator(config)
    return generator.generate_spdx3()
