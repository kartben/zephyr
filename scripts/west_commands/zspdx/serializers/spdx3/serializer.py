# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import json
import os
import re
from datetime import datetime, timezone

from spdx_python_model import v3_0_1 as spdx
from west import log

from zspdx.model import ComponentPurpose, SBOMComponent, SBOMData, SBOMFile, SBOMRelationship
from zspdx.spdxids import getUniqueFileID

CPE23TYPE_REGEX = (
    r'^cpe:2\.3:[aho\*\-](:(((\?*|\*?)([a-zA-Z0-9\-\._]|(\\[\\\*\?!"#$$%&\'\(\)\+,\/:;<=>@\[\]\^'
    r"`\{\|}~]))+(\?*|\*?))|[\*\-])){5}(:(([a-zA-Z]{2,3}(-([a-zA-Z]{2}|[0-9]{3}))?)|[\*\-]))(:(((\?*"
    r'|\*?)([a-zA-Z0-9\-\._]|(\\[\\\*\?!"#$$%&\'\(\)\+,\/:;<=>@\[\]\^`\{\|}~]))+(\?*|\*?))|[\*\-])){4}$'
)
PURL_REGEX = r"^pkg:.+(\/.+)?\/.+(@.+)?(\?.+)?(#.+)?$"


class SPDX3Serializer:
    """Serializer that converts SBOMData to SPDX 3.0 format (JSON-LD and JSON)."""

    def __init__(self, sbom_data: SBOMData, spdx_version=None):
        self.sbom_data = sbom_data
        self.spdx_version = spdx_version  # Not used for SPDX 3.0, but kept for API consistency

        # Track SPDX 3.0 elements
        self.elements = []  # All SPDX3 elements (packages, files, relationships, etc.)
        self.component_elements = {}  # component_name -> software_Package
        self.file_elements = {}  # file_path -> software_File
        self.relationship_elements = []  # List of Relationship objects

        # Shared objects
        self.tool = None
        self.creation_info = None
        self.document = None

        # Track file IDs for uniqueness
        self.filename_counts = sbom_data.filename_counts.copy() if sbom_data.filename_counts else {}

    def _normalize_name(self, name):
        """Normalize component/file names for use in URIs."""
        return name.replace("_", "-").replace(" ", "-")

    def _generate_package_id(self, component_name: str) -> str:
        """Generate URI-based ID for a package."""
        normalized = self._normalize_name(component_name)
        namespace = self.sbom_data.namespace_prefix.rstrip("/")
        return f"{namespace}/packages/{normalized}"

    def _generate_file_id(self, file_path: str) -> str:
        """Generate URI-based ID for a file."""
        filename_only = os.path.basename(file_path)
        unique_id = getUniqueFileID(filename_only, self.filename_counts)
        # Remove "SPDXRef-" prefix and normalize
        normalized_id = unique_id.replace("SPDXRef-", "").replace("_", "-")
        namespace = self.sbom_data.namespace_prefix.rstrip("/")
        return f"{namespace}/files/{normalized_id}"

    def _generate_relationship_id(self, index: int) -> str:
        """Generate URI-based ID for a relationship."""
        namespace = self.sbom_data.namespace_prefix.rstrip("/")
        return f"{namespace}/relationships/{index}"

    def _purpose_to_spdx3(self, purpose: ComponentPurpose) -> spdx.software_SoftwarePurpose:
        """Convert ComponentPurpose enum to SPDX 3.0 SoftwarePurpose."""
        purpose_map = {
            ComponentPurpose.APPLICATION: spdx.software_SoftwarePurpose.application,
            ComponentPurpose.LIBRARY: spdx.software_SoftwarePurpose.library,
            ComponentPurpose.SOURCE: spdx.software_SoftwarePurpose.source,
            ComponentPurpose.FILE: spdx.software_SoftwarePurpose.file,
        }
        return purpose_map.get(purpose, spdx.software_SoftwarePurpose.library)

    def _initialize_shared_objects(self):
        """Initialize shared Tool and CreationInfo objects."""
        if self.tool is None:
            self.tool = spdx.Agent()
            namespace = self.sbom_data.namespace_prefix.rstrip("/")
            self.tool._id = f"{namespace}/agents/west-spdx-tool"
            self.tool.name = "West SPDX Tool"
            self.tool.creationInfo = f"{namespace}/creationinfo"
            self.elements.append(self.tool)

        if self.creation_info is None:
            self.creation_info = spdx.CreationInfo()
            namespace = self.sbom_data.namespace_prefix.rstrip("/")
            self.creation_info._id = f"{namespace}/creationinfo"
            self.creation_info.created = datetime.now(timezone.utc)
            self.creation_info.createdBy.append(self.tool._id)
            self.creation_info.specVersion = "3.0.1"
            self.elements.append(self.creation_info)

    def _create_software_package(self, component: SBOMComponent) -> spdx.software_Package:
        """Convert SBOMComponent to SPDX 3.0 software_Package."""
        package = spdx.software_Package()
        package._id = self._generate_package_id(component.name)
        package.name = component.name
        package.creationInfo = self.creation_info._id
        package.software_primaryPurpose = self._purpose_to_spdx3(component.purpose)

        # Version
        if component.version:
            package.software_packageVersion = component.version
        elif component.revision:
            package.software_packageVersion = component.revision

        # Download location
        if component.url:
            if component.revision:
                package.software_downloadLocation = f"git+{component.url}@{component.revision}"
            else:
                package.software_downloadLocation = component.url
        else:
            package.software_downloadLocation = "NOASSERTION"

        # Copyright
        package.software_copyrightText = component.copyright_text or "NOASSERTION"

        # License information will be added via relationships after package creation

        # External references (CPE, PURL)
        for ref in component.external_references:
            if not ref or not isinstance(ref, str):
                log.wrn(f"Invalid external reference: {ref}")
                continue
            if re.fullmatch(CPE23TYPE_REGEX, ref):
                ext_id = spdx.ExternalIdentifier()
                ext_id.externalIdentifierType = spdx.ExternalIdentifierType.cpe23
                ext_id.identifier = ref
                package.externalIdentifier.append(ext_id)
            elif re.fullmatch(PURL_REGEX, ref):
                ext_id = spdx.ExternalIdentifier()
                ext_id.externalIdentifierType = spdx.ExternalIdentifierType.packageUrl
                ext_id.identifier = ref
                package.externalIdentifier.append(ext_id)
            else:
                log.wrn(f"Unknown external reference format: {ref}")

        # Verification code (if available)
        # Note: SPDX 3.0 may handle verification codes differently than SPDX 2.x
        # For now, we'll skip setting it directly as the property may not exist
        # TODO: Investigate SPDX 3.0 verification code handling

        self.elements.append(package)
        self.component_elements[component.name] = package
        return package

    def _create_software_file(self, file_obj: SBOMFile) -> spdx.software_File:
        """Convert SBOMFile to SPDX 3.0 software_File."""
        file_element = spdx.software_File()
        file_element._id = self._generate_file_id(file_obj.path)
        file_element.name = file_obj.relative_path or os.path.basename(file_obj.path)
        file_element.creationInfo = self.creation_info._id
        file_element.software_fileKind = spdx.software_FileKindType.file

        # File purpose (default to file)
        file_element.software_primaryPurpose = spdx.software_SoftwarePurpose.file

        # Copyright
        file_element.software_copyrightText = file_obj.copyright_text or "NOASSERTION"

        # Hashes - SPDX 3.0 uses verifiedUsing with Hash (which is a type of IntegrityMethod)
        for hash_type, hash_value in file_obj.hashes.items():
            if hash_value:
                hash_obj = spdx.Hash()
                if hash_type == "SHA1":
                    hash_obj.algorithm = spdx.HashAlgorithm.sha1
                elif hash_type == "SHA256":
                    hash_obj.algorithm = spdx.HashAlgorithm.sha256
                elif hash_type == "MD5":
                    hash_obj.algorithm = spdx.HashAlgorithm.md5
                else:
                    log.wrn(f"Unknown hash algorithm: {hash_type}")
                    continue
                hash_obj.hashValue = hash_value
                file_element.verifiedUsing.append(hash_obj)

        # License information will be added via relationships after file creation

        self.elements.append(file_element)
        self.file_elements[file_obj.path] = file_element
        return file_element

    def _map_relationship_type(self, rel_type: str) -> spdx.RelationshipType:
        """Map relationship type string to SPDX 3.0 RelationshipType enum."""
        # Map SPDX 2.x relationship types to SPDX 3.0 RelationshipType
        type_map = {
            "GENERATED_FROM": spdx.RelationshipType.generates,
            "HAS_PREREQUISITE": spdx.RelationshipType.dependsOn,
            "STATIC_LINK": spdx.RelationshipType.hasStaticLink,
            "CONTAINS": spdx.RelationshipType.contains,
            "DESCRIBES": spdx.RelationshipType.describes,
            "DEPENDS_ON": spdx.RelationshipType.dependsOn,
            "DYNAMIC_LINK": spdx.RelationshipType.hasDynamicLink,
            "BUILD_TOOL_OF": spdx.RelationshipType.usesTool,
            "DEV_TOOL_OF": spdx.RelationshipType.usesTool,
            "TEST_TOOL_OF": spdx.RelationshipType.usesTool,
            "OTHER": spdx.RelationshipType.other,
        }
        return type_map.get(rel_type, spdx.RelationshipType.other)

    def _get_element_id(self, element):
        """Get SPDX 3.0 element ID from various element types."""
        if isinstance(element, SBOMComponent):
            if element.name in self.component_elements:
                return self.component_elements[element.name]._id
        elif isinstance(element, SBOMFile):
            if element.path in self.file_elements:
                return self.file_elements[element.path]._id
        elif isinstance(element, str):
            # Try to resolve as component name first, then file path
            if element in self.component_elements:
                return self.component_elements[element]._id
            elif element in self.file_elements:
                return self.file_elements[element]._id
        return None

    def _create_relationship(self, rel: SBOMRelationship) -> spdx.Relationship:
        """Convert SBOMRelationship to SPDX 3.0 Relationship."""
        # Get from_element ID
        from_id = self._get_element_id(rel.from_element)
        if not from_id:
            log.wrn(f"Could not resolve from_element for relationship: {rel.relationship_type}")
            return None

        # Get to_elements IDs
        to_elements = rel.to_elements if isinstance(rel.to_elements, list) else [rel.to_elements]
        to_ids = []
        for to_elem in to_elements:
            to_id = self._get_element_id(to_elem)
            if to_id:
                to_ids.append(to_id)
            else:
                log.wrn(f"Could not resolve to_element for relationship: {rel.relationship_type}")

        if not to_ids:
            log.wrn(f"No valid to_elements found for relationship: {rel.relationship_type}")
            return None

        relationship = spdx.Relationship()
        relationship._id = self._generate_relationship_id(len(self.relationship_elements))
        relationship.relationshipType = self._map_relationship_type(rel.relationship_type)
        relationship.from_ = from_id
        relationship.to = to_ids
        relationship.creationInfo = self.creation_info._id

        self.elements.append(relationship)
        self.relationship_elements.append(relationship)
        return relationship

    def _get_standard_licenses(self):
        """Get set of standard SPDX license IDs."""
        # Import here to avoid circular dependency
        from zspdx.licenses import LICENSES
        return set(LICENSES)

    def _create_license_expression(self, license_str: str) -> spdx.simplelicensing_LicenseExpression:
        """Create a license expression object and add it to elements."""
        if not license_str or license_str == "NOASSERTION":
            return None

        license_expr = spdx.simplelicensing_LicenseExpression()
        standard_licenses = self._get_standard_licenses()

        # Check if it's a standard license ID
        if license_str in standard_licenses:
            license_expr._id = f"https://spdx.org/licenses/{license_str}"
        else:
            # Custom license - use a namespace-based ID
            namespace = self.sbom_data.namespace_prefix.rstrip("/")
            # Normalize the license string for use in URI
            normalized = license_str.replace(" ", "-").replace("(", "").replace(")", "")
            license_expr._id = f"{namespace}/licenses/{normalized}"

        license_expr.simplelicensing_licenseExpression = license_str
        license_expr.creationInfo = self.creation_info._id

        # Add to elements if not already present
        existing_ids = {elem._id for elem in self.elements if hasattr(elem, '_id')}
        if license_expr._id not in existing_ids:
            self.elements.append(license_expr)

        return license_expr

    def _create_document(self) -> spdx.SpdxDocument:
        """Create the main SPDX 3.0 document."""
        self._initialize_shared_objects()

        document = spdx.SpdxDocument()
        namespace = self.sbom_data.namespace_prefix.rstrip("/")
        document._id = f"{namespace}/documents/zephyr-spdx3"
        document.name = "Zephyr SPDX 3.0 SBOM"
        document.creationInfo = self.creation_info

        # Set data license
        data_license = self._create_license_expression("CC0-1.0")
        if data_license:
            document.dataLicense = data_license._id

        # Add profile conformance
        document.profileConformance.append(spdx.ProfileIdentifierType.core)

        # Add all elements to document
        # Note: CreationInfo is not an Element, and document shouldn't include itself
        # The encoder should handle Element objects and serialize them as references
        document_id = document._id
        for element in self.elements:
            # Only add Element types (not CreationInfo or other non-Element types)
            # Also exclude the document itself from its own element list
            if (isinstance(element, spdx.Element) and 
                hasattr(element, '_id') and element._id and 
                element._id != document_id and
                not isinstance(element, spdx.SpdxDocument)):  # Explicitly exclude SpdxDocument
                document.element.append(element)

        # Set root elements (main packages: app-sources, zephyr-sources, etc.)
        root_component_names = ["app-sources", "zephyr-sources", "sdk-sources"]
        for comp_name in root_component_names:
            if comp_name in self.component_elements:
                document.rootElement.append(self.component_elements[comp_name])

        # Also add modules-deps components as root elements
        for comp_name, component in self.sbom_data.components.items():
            if comp_name.endswith("-deps") and comp_name in self.component_elements:
                document.rootElement.append(self.component_elements[comp_name])

        self.elements.append(document)
        self.document = document
        return document

    def serialize(self, output_dir: str) -> bool:
        """Serialize SBOMData to SPDX 3.0 format (JSON-LD and JSON)."""
        try:
            # Validate input
            if not self.sbom_data:
                log.err("SBOMData is None or empty")
                return False

            if not self.sbom_data.namespace_prefix:
                log.err("Namespace prefix is required for SPDX 3.0")
                return False

            if not os.path.exists(output_dir):
                log.err(f"Output directory does not exist: {output_dir}")
                return False

            # Initialize shared objects
            self._initialize_shared_objects()

            # Create all software packages from components
            if not self.sbom_data.components:
                log.wrn("No components found in SBOM data")
            else:
                for component in self.sbom_data.components.values():
                    if not component.name:
                        log.wrn("Skipping component with empty name")
                        continue
                    self._create_software_package(component)

            # Create all software files
            if not self.sbom_data.files:
                log.wrn("No files found in SBOM data")
            else:
                for file_obj in self.sbom_data.files.values():
                    if not file_obj.path:
                        log.wrn("Skipping file with empty path")
                        continue
                    self._create_software_file(file_obj)

            # Create relationships from components
            for component in self.sbom_data.components.values():
                for rel in component.relationships:
                    created_rel = self._create_relationship(rel)
                    if not created_rel:
                        log.wrn(f"Failed to create relationship from component {component.name}")

            # Create relationships from files
            for file_obj in self.sbom_data.files.values():
                for rel in file_obj.relationships:
                    created_rel = self._create_relationship(rel)
                    if not created_rel:
                        log.wrn(f"Failed to create relationship from file {file_obj.path}")

            # Create top-level relationships
            for rel in self.sbom_data.relationships:
                created_rel = self._create_relationship(rel)
                if not created_rel:
                    log.wrn("Failed to create top-level relationship")

            # Create CONTAINS relationships for files in packages
            for component in self.sbom_data.components.values():
                package = self.component_elements.get(component.name)
                if package:
                    for file_obj in component.files.values():
                        file_element = self.file_elements.get(file_obj.path)
                        if file_element:
                            contains_rel = spdx.Relationship()
                            contains_rel._id = self._generate_relationship_id(len(self.relationship_elements))
                            contains_rel.relationshipType = spdx.RelationshipType.contains
                            contains_rel.from_ = package._id
                            contains_rel.to = [file_element._id]
                            contains_rel.creationInfo = self.creation_info._id
                            self.elements.append(contains_rel)
                            self.relationship_elements.append(contains_rel)

            # Create license relationships for packages
            for component in self.sbom_data.components.values():
                package = self.component_elements.get(component.name)
                if package:
                    # Create hasConcludedLicense relationship
                    if component.concluded_license and component.concluded_license != "NOASSERTION":
                        license_expr = self._create_license_expression(component.concluded_license)
                        if license_expr:
                            rel = spdx.Relationship()
                            rel._id = self._generate_relationship_id(len(self.relationship_elements))
                            rel.relationshipType = spdx.RelationshipType.hasConcludedLicense
                            rel.from_ = package._id
                            rel.to = [license_expr._id]
                            rel.creationInfo = self.creation_info._id
                            self.elements.append(rel)
                            self.relationship_elements.append(rel)

                    # Create hasDeclaredLicense relationship
                    if component.declared_license and component.declared_license != "NOASSERTION":
                        license_expr = self._create_license_expression(component.declared_license)
                        if license_expr:
                            rel = spdx.Relationship()
                            rel._id = self._generate_relationship_id(len(self.relationship_elements))
                            rel.relationshipType = spdx.RelationshipType.hasDeclaredLicense
                            rel.from_ = package._id
                            rel.to = [license_expr._id]
                            rel.creationInfo = self.creation_info._id
                            self.elements.append(rel)
                            self.relationship_elements.append(rel)

            # Create license relationships for files
            for file_obj in self.sbom_data.files.values():
                file_element = self.file_elements.get(file_obj.path)
                if file_element:
                    # Create hasConcludedLicense relationship
                    if file_obj.concluded_license and file_obj.concluded_license != "NOASSERTION":
                        license_expr = self._create_license_expression(file_obj.concluded_license)
                        if license_expr:
                            rel = spdx.Relationship()
                            rel._id = self._generate_relationship_id(len(self.relationship_elements))
                            rel.relationshipType = spdx.RelationshipType.hasConcludedLicense
                            rel.from_ = file_element._id
                            rel.to = [license_expr._id]
                            rel.creationInfo = self.creation_info._id
                            self.elements.append(rel)
                            self.relationship_elements.append(rel)

                    # Create hasDeclaredLicense relationships for each license in file
                    if file_obj.license_info_in_file:
                        for lic in file_obj.license_info_in_file:
                            if lic != "NONE":
                                license_expr = self._create_license_expression(lic)
                                if license_expr:
                                    rel = spdx.Relationship()
                                    rel._id = self._generate_relationship_id(len(self.relationship_elements))
                                    rel.relationshipType = spdx.RelationshipType.hasDeclaredLicense
                                    rel.from_ = file_element._id
                                    rel.to = [license_expr._id]
                                    rel.creationInfo = self.creation_info._id
                                    self.elements.append(rel)
                                    self.relationship_elements.append(rel)

            # Create the document
            self._create_document()

            # Serialize to JSON-LD
            jsonld_path = os.path.join(output_dir, "zephyr-spdx3.jsonld")
            self._serialize_to_jsonld(jsonld_path)

            # Serialize to JSON
            json_path = os.path.join(output_dir, "zephyr-spdx3.json")
            self._serialize_to_json(json_path)

            log.inf(f"SPDX 3.0 documents written to {output_dir}")
            return True

        except Exception as e:
            log.err(f"Failed to serialize SPDX 3.0 document: {e}")
            import traceback
            log.dbg(traceback.format_exc())
            return False

    def _serialize_to_jsonld(self, output_path: str):
        """Serialize to JSON-LD format."""
        elements_data = []
        for element in self.elements:
            try:
                encoder = spdx.JSONLDEncoder()
                state = spdx.EncodeState()
                element.encode(encoder, state)
                if encoder.data:
                    elements_data.append(encoder.data)
            except Exception as e:
                log.wrn(f"Failed to encode element {getattr(element, '_id', 'unknown')}: {e}")

        complete_dict = {
            "@context": "https://spdx.org/rdf/3.0.1/spdx-context.jsonld",
            "@graph": elements_data,
        }

        # Post-process: Convert document.element and rootElement from full objects to ID references
        # This is required for validators that expect element references as IDs
        for elem in elements_data:
            if elem.get("type") == "SpdxDocument":
                # Convert element references to IDs
                if "element" in elem:
                    element_refs = elem["element"]
                    element_ids = []
                    for ref in element_refs:
                        if isinstance(ref, dict):
                            # Extract the ID from the object
                            elem_id = ref.get("spdxId") or ref.get("@id")
                            if elem_id:
                                element_ids.append(elem_id)
                            else:
                                log.wrn(f"Could not extract ID from element object: {ref}")
                        elif isinstance(ref, str):
                            # Already an ID
                            element_ids.append(ref)
                        else:
                            log.wrn(f"Unexpected element reference type: {type(ref)}")
                    elem["element"] = element_ids

                # Convert rootElement references to IDs
                if "rootElement" in elem:
                    root_refs = elem["rootElement"]
                    root_ids = []
                    for ref in root_refs:
                        if isinstance(ref, dict):
                            # Extract the ID from the object
                            root_id = ref.get("spdxId") or ref.get("@id")
                            if root_id:
                                root_ids.append(root_id)
                            else:
                                log.wrn(f"Could not extract ID from rootElement object: {ref}")
                        elif isinstance(ref, str):
                            # Already an ID
                            root_ids.append(ref)
                        else:
                            log.wrn(f"Unexpected rootElement reference type: {type(ref)}")
                    elem["rootElement"] = root_ids

        with open(output_path, "w") as f:
            json.dump(complete_dict, f, indent=2)

        log.inf(f"Written SPDX 3.0 JSON-LD to {output_path}")

    def _serialize_to_json(self, output_path: str):
        """Serialize to plain JSON format."""
        # For JSON format, use JSONLDEncoder but create a simpler structure
        # Note: SPDX 3.0 spec primarily uses JSON-LD, but we'll provide a JSON version
        # that's easier to parse without JSON-LD context
        elements_data = []
        for element in self.elements:
            try:
                encoder = spdx.JSONLDEncoder()
                state = spdx.EncodeState()
                element.encode(encoder, state)
                if encoder.data:
                    elements_data.append(encoder.data)
            except Exception as e:
                log.wrn(f"Failed to encode element {getattr(element, '_id', 'unknown')}: {e}")

        complete_dict = {
            "spdxVersion": "SPDX-3.0.1",
            "elements": elements_data,
        }

        with open(output_path, "w") as f:
            json.dump(complete_dict, f, indent=2)

        log.inf(f"Written SPDX 3.0 JSON to {output_path}")
