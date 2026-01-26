# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import json
import os
import re
from collections import defaultdict
from datetime import datetime, timezone

from spdx_python_model import v3_0_1 as spdx
from west import log

from zspdx.model import (
    ComponentPurpose,
    SBOMComponent,
    SBOMData,
    SBOMExternalReference,
    SBOMFile,
    SBOMRelationship,
)
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
        self.documents = {}  # doc_name -> SpdxDocument

        # Build tools and information
        self.build_tools = {}  # tool_name -> Tool/Agent
        # Extract build information from metadata if available
        self.build_info = sbom_data.metadata.get('build_info', {}) if sbom_data.metadata else {}

        # Track file IDs for uniqueness
        self.filename_counts = {}

        # Group components into documents (similar to SPDX 2.x)
        self.component_groups = self._group_components_into_documents()

    def _normalize_name(self, name):
        """Normalize component/file names for use in URIs."""
        return name.replace("_", "-").replace(" ", "-")

    def _group_components_into_documents(self):
        """Group components into SPDX 3.0 documents based on their names/purposes."""
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

    def _generate_package_id(self, component_name: str) -> str:
        """Generate URI-based ID for a package."""
        normalized = self._normalize_name(component_name)
        namespace = self.sbom_data.id_namespace.rstrip("/")
        return f"{namespace}/packages/{normalized}"

    def _generate_file_id(self, file_path: str) -> str:
        """Generate URI-based ID for a file."""
        filename_only = os.path.basename(file_path)
        unique_id = getUniqueFileID(filename_only, self.filename_counts)
        # Remove "SPDXRef-" prefix and normalize
        normalized_id = unique_id.replace("SPDXRef-", "").replace("_", "-")
        namespace = self.sbom_data.id_namespace.rstrip("/")
        return f"{namespace}/files/{normalized_id}"

    def _generate_relationship_id(self, index: int) -> str:
        """Generate URI-based ID for a relationship."""
        namespace = self.sbom_data.id_namespace.rstrip("/")
        return f"{namespace}/relationships/{index}"

    def _purpose_to_spdx3(self, purpose) -> spdx.software_SoftwarePurpose:
        """Convert component purpose to SPDX 3.0 SoftwarePurpose."""
        if isinstance(purpose, ComponentPurpose):
            purpose_str = purpose.value
        elif isinstance(purpose, str):
            purpose_str = purpose.strip().upper()
        else:
            return spdx.software_SoftwarePurpose.library

        purpose_map = {
            "APPLICATION": "application",
            "FRAMEWORK": "framework",
            "LIBRARY": "library",
            "CONTAINER": "container",
            "OPERATING_SYSTEM": "operatingSystem",
            "DEVICE": "device",
            "FIRMWARE": "firmware",
            "SOURCE": "source",
            "ARCHIVE": "archive",
            "FILE": "file",
            "INSTALL": "installation",
            "PLATFORM": "platform",
            "OTHER": "other",
            "UNKNOWN": "other",
        }
        attr = purpose_map.get(purpose_str)
        if attr and hasattr(spdx.software_SoftwarePurpose, attr):
            return getattr(spdx.software_SoftwarePurpose, attr)
        return spdx.software_SoftwarePurpose.library

    def _create_build_tools(self):
        """Create Tool/Agent elements for build tools (CMake, compilers, etc.)."""
        namespace = self.sbom_data.id_namespace.rstrip("/")
        
        # Create CMake tool
        if self.build_info.get("cmake_version") or self.build_info.get("cmake_generator"):
            cmake_tool = spdx.Agent()
            cmake_tool._id = f"{namespace}/agents/cmake"
            cmake_name = "CMake"
            if self.build_info.get("cmake_version"):
                cmake_name += f" {self.build_info['cmake_version']}"
            if self.build_info.get("cmake_generator"):
                cmake_name += f" ({self.build_info['cmake_generator']})"
            cmake_tool.name = cmake_name
            cmake_tool.creationInfo = self.creation_info._id
            # Add build type as external identifier
            if self.build_info.get("cmake_build_type"):
                ext_id = spdx.ExternalIdentifier()
                ext_id.externalIdentifierType = spdx.ExternalIdentifierType.other
                ext_id.identifier = f"build-type:{self.build_info['cmake_build_type']}"
                cmake_tool.externalIdentifier.append(ext_id)
            self.elements.append(cmake_tool)
            self.build_tools["cmake"] = cmake_tool
        
        # Create C compiler tool
        compiler_path = self.build_info.get("cmake_compiler", "")
        if compiler_path:
            compiler_name = os.path.basename(compiler_path)
            compiler_tool = spdx.Agent()
            compiler_tool._id = f"{namespace}/agents/c-compiler"
            compiler_tool.name = f"C Compiler ({compiler_name})"
            compiler_tool.creationInfo = self.creation_info._id
            # Add external identifier for compiler path
            ext_id = spdx.ExternalIdentifier()
            ext_id.externalIdentifierType = spdx.ExternalIdentifierType.other
            ext_id.identifier = compiler_path
            compiler_tool.externalIdentifier.append(ext_id)
            # Add system processor if available
            if self.build_info.get("cmake_system_processor"):
                sys_ext_id = spdx.ExternalIdentifier()
                sys_ext_id.externalIdentifierType = spdx.ExternalIdentifierType.other
                sys_ext_id.identifier = f"target-arch:{self.build_info['cmake_system_processor']}"
                compiler_tool.externalIdentifier.append(sys_ext_id)
            self.elements.append(compiler_tool)
            self.build_tools["c-compiler"] = compiler_tool
        
        # Create C++ compiler tool
        cxx_compiler_path = self.build_info.get("cmake_cxx_compiler", "")
        if cxx_compiler_path and cxx_compiler_path != compiler_path:
            cxx_compiler_name = os.path.basename(cxx_compiler_path)
            cxx_compiler_tool = spdx.Agent()
            cxx_compiler_tool._id = f"{namespace}/agents/cxx-compiler"
            cxx_compiler_tool.name = f"C++ Compiler ({cxx_compiler_name})"
            cxx_compiler_tool.creationInfo = self.creation_info._id
            ext_id = spdx.ExternalIdentifier()
            ext_id.externalIdentifierType = spdx.ExternalIdentifierType.other
            ext_id.identifier = cxx_compiler_path
            cxx_compiler_tool.externalIdentifier.append(ext_id)
            self.elements.append(cxx_compiler_tool)
            self.build_tools["cxx-compiler"] = cxx_compiler_tool

    def _initialize_shared_objects(self):
        """Initialize shared Tool and CreationInfo objects."""
        if self.tool is None:
            self.tool = spdx.Agent()
            namespace = self.sbom_data.id_namespace.rstrip("/")
            self.tool._id = f"{namespace}/agents/west-spdx-tool"
            self.tool.name = "West SPDX Tool"
            self.tool.creationInfo = f"{namespace}/creationinfo"
            self.elements.append(self.tool)

        if self.creation_info is None:
            self.creation_info = spdx.CreationInfo()
            namespace = self.sbom_data.id_namespace.rstrip("/")
            self.creation_info._id = f"{namespace}/creationinfo"
            self.creation_info.created = datetime.now(timezone.utc)
            self.creation_info.createdBy.append(self.tool._id)
            self.creation_info.specVersion = "3.0.1"
            self.elements.append(self.creation_info)
        
        # Create build tool agents if build info is available
        self._create_build_tools()

    def _normalize_external_reference(self, ref):
        """Return (ref_type, locator) for external references."""
        if isinstance(ref, SBOMExternalReference):
            return (ref.reference_type or "").lower(), ref.locator
        return "", ref

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
            ref_type, locator = self._normalize_external_reference(ref)
            if not locator or not isinstance(locator, str):
                log.wrn(f"Invalid external reference: {locator}")
                continue
            if ref_type in ("cpe", "cpe23", "cpe23type") or re.fullmatch(CPE23TYPE_REGEX, locator):
                ext_id = spdx.ExternalIdentifier()
                ext_id.externalIdentifierType = spdx.ExternalIdentifierType.cpe23
                ext_id.identifier = locator
                package.externalIdentifier.append(ext_id)
            elif ref_type in ("purl", "packageurl", "package-url") or re.fullmatch(PURL_REGEX, locator):
                ext_id = spdx.ExternalIdentifier()
                ext_id.externalIdentifierType = spdx.ExternalIdentifierType.packageUrl
                ext_id.identifier = locator
                package.externalIdentifier.append(ext_id)
            else:
                log.wrn(f"Unknown external reference format: {locator}")

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
            namespace = self.sbom_data.id_namespace.rstrip("/")
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

    def _create_document(self, doc_name: str, components: list) -> spdx.SpdxDocument:
        """Create an SPDX 3.0 document for a specific group of components."""
        self._initialize_shared_objects()

        document = spdx.SpdxDocument()
        namespace = self.sbom_data.id_namespace.rstrip("/")
        document._id = f"{namespace}/documents/{doc_name}-spdx3"
        
        # Set document name based on type
        doc_names = {
            "app": "Zephyr Application SPDX 3.0 SBOM",
            "zephyr": "Zephyr RTOS SPDX 3.0 SBOM",
            "build": "Zephyr Build Artifacts SPDX 3.0 SBOM",
            "modules-deps": "Zephyr Module Dependencies SPDX 3.0 SBOM",
            "sdk": "Zephyr SDK SPDX 3.0 SBOM",
        }
        document.name = doc_names.get(doc_name, f"Zephyr {doc_name.capitalize()} SPDX 3.0 SBOM")
        document.creationInfo = self.creation_info

        # Set data license
        data_license = self._create_license_expression("CC0-1.0")
        if data_license:
            document.dataLicense = data_license._id

        # Add profile conformance
        document.profileConformance.append(spdx.ProfileIdentifierType.core)
        # Add build profile if we have build information
        if self.build_info:
            document.profileConformance.append(spdx.ProfileIdentifierType.build)

        # Collect elements that belong to this document
        document_component_names = {comp.name for comp in components}
        document_component_ids = set()
        document_file_paths = set()
        document_file_ids = set()
        
        # Get component and file IDs for this document
        for component in components:
            if component.name in self.component_elements:
                document_component_ids.add(self.component_elements[component.name]._id)
            for file_obj in component.files.values():
                document_file_paths.add(file_obj.path)
                if file_obj.path in self.file_elements:
                    document_file_ids.add(self.file_elements[file_obj.path]._id)

        # Collect all element IDs that belong to this document
        document_element_ids = set(document_component_ids)
        document_element_ids.update(document_file_ids)
        
        # Collect license expression IDs used by our components and files
        license_ids = set()
        for component in components:
            if component.concluded_license and component.concluded_license != "NOASSERTION":
                license_expr = self._create_license_expression(component.concluded_license)
                if license_expr:
                    license_ids.add(license_expr._id)
            if component.declared_license and component.declared_license != "NOASSERTION":
                license_expr = self._create_license_expression(component.declared_license)
                if license_expr:
                    license_ids.add(license_expr._id)
            for file_obj in component.files.values():
                if file_obj.concluded_license and file_obj.concluded_license != "NOASSERTION":
                    license_expr = self._create_license_expression(file_obj.concluded_license)
                    if license_expr:
                        license_ids.add(license_expr._id)
                for lic in file_obj.license_info_in_file:
                    if lic != "NONE":
                        license_expr = self._create_license_expression(lic)
                        if license_expr:
                            license_ids.add(license_expr._id)
        document_element_ids.update(license_ids)

        # Collect relationship IDs that involve our elements
        relationship_ids = set()
        for rel in self.relationship_elements:
            from_id = getattr(rel, 'from_', None)
            to_ids = getattr(rel, 'to', [])
            if from_id in document_element_ids or any(to_id in document_element_ids for to_id in to_ids):
                relationship_ids.add(rel._id)
        document_element_ids.update(relationship_ids)

        # Add tool and data license
        if self.tool:
            document_element_ids.add(self.tool._id)
        # Add build tools
        for tool in self.build_tools.values():
            document_element_ids.add(tool._id)
        data_license = self._create_license_expression("CC0-1.0")
        if data_license:
            document_element_ids.add(data_license._id)

        # Add relevant elements to document
        document_id = document._id
        for element in self.elements:
            # Only add Element types (not CreationInfo or other non-Element types)
            # Also exclude the document itself from its own element list
            if (isinstance(element, spdx.Element) and 
                hasattr(element, '_id') and element._id and 
                element._id != document_id and
                not isinstance(element, spdx.SpdxDocument)):
                
                if element._id in document_element_ids:
                    document.element.append(element)

        # Set root elements (components in this document)
        for component in components:
            if component.name in self.component_elements:
                document.rootElement.append(self.component_elements[component.name])

        self.elements.append(document)
        self.documents[doc_name] = document
        return document

    def serialize(self, output_dir: str) -> bool:
        """Serialize SBOMData to SPDX 3.0 format (JSON-LD and JSON)."""
        try:
            # Validate input
            if not self.sbom_data:
                log.err("SBOMData is None or empty")
                return False

            if not self.sbom_data.id_namespace:
                log.err("ID namespace is required for SPDX 3.0")
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

            # Create buildToolOf relationships for build artifacts
            # Link build tools to build artifact files
            for component in self.sbom_data.components.values():
                # Only process build target components (not source components)
                if component.name not in ["app-sources", "zephyr-sources", "sdk-sources"] and not component.name.endswith("-deps"):
                    package = self.component_elements.get(component.name)
                    if package and component.target_build_file:
                        build_file = self.file_elements.get(component.target_build_file.path)
                        if build_file:
                            # Link CMake to build file
                            if "cmake" in self.build_tools:
                                rel = spdx.Relationship()
                                rel._id = self._generate_relationship_id(len(self.relationship_elements))
                                rel.relationshipType = spdx.RelationshipType.usesTool
                                rel.from_ = build_file._id
                                rel.to = [self.build_tools["cmake"]._id]
                                rel.creationInfo = self.creation_info._id
                                self.elements.append(rel)
                                self.relationship_elements.append(rel)
                            
                            # Link compiler to build file
                            if "c-compiler" in self.build_tools:
                                rel = spdx.Relationship()
                                rel._id = self._generate_relationship_id(len(self.relationship_elements))
                                rel.relationshipType = spdx.RelationshipType.usesTool
                                rel.from_ = build_file._id
                                rel.to = [self.build_tools["c-compiler"]._id]
                                rel.creationInfo = self.creation_info._id
                                self.elements.append(rel)
                                self.relationship_elements.append(rel)

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

            # Create documents for each group
            for doc_name, components in self.component_groups.items():
                if not components:
                    continue
                self._create_document(doc_name, components)

            # Serialize each document to separate files
            for doc_name, document in self.documents.items():
                # Serialize to JSON-LD
                jsonld_path = os.path.join(output_dir, f"{doc_name}-spdx3.jsonld")
                self._serialize_document_to_jsonld(document, jsonld_path)

                # Serialize to JSON
                json_path = os.path.join(output_dir, f"{doc_name}-spdx3.json")
                self._serialize_document_to_json(document, json_path)

            log.inf(f"SPDX 3.0 documents written to {output_dir}")
            return True

        except Exception as e:
            log.err(f"Failed to serialize SPDX 3.0 document: {e}")
            import traceback
            log.dbg(traceback.format_exc())
            return False

    def _serialize_document_to_jsonld(self, document: spdx.SpdxDocument, output_path: str):
        """Serialize a single document to JSON-LD format."""
        # Collect all elements referenced by this document
        # document.element contains Element objects, so extract their IDs
        document_element_ids = set()
        if hasattr(document, 'element'):
            for elem in document.element:
                if hasattr(elem, '_id') and elem._id:
                    document_element_ids.add(elem._id)
        
        # Always include the document itself, creation info, and tool
        elements_to_serialize = [document]
        if self.creation_info:
            elements_to_serialize.append(self.creation_info)
        if self.tool:
            elements_to_serialize.append(self.tool)
        
        # Add all elements referenced by the document
        for elem_id in document_element_ids:
            # Find the element in our elements list
            for elem in self.elements:
                if hasattr(elem, '_id') and elem._id == elem_id:
                    if elem not in elements_to_serialize:
                        elements_to_serialize.append(elem)
                    break
        
        # Serialize all elements
        elements_data = []
        for element in elements_to_serialize:
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
        for elem in elements_data:
            if elem.get("type") == "SpdxDocument":
                # Convert element references to IDs
                if "element" in elem:
                    element_refs = elem["element"]
                    element_ids = []
                    for ref in element_refs:
                        if isinstance(ref, dict):
                            elem_id = ref.get("spdxId") or ref.get("@id")
                            if elem_id:
                                element_ids.append(elem_id)
                        elif isinstance(ref, str):
                            element_ids.append(ref)
                    elem["element"] = element_ids

                # Convert rootElement references to IDs
                if "rootElement" in elem:
                    root_refs = elem["rootElement"]
                    root_ids = []
                    for ref in root_refs:
                        if isinstance(ref, dict):
                            root_id = ref.get("spdxId") or ref.get("@id")
                            if root_id:
                                root_ids.append(root_id)
                        elif isinstance(ref, str):
                            root_ids.append(ref)
                    elem["rootElement"] = root_ids

        with open(output_path, "w") as f:
            json.dump(complete_dict, f, indent=2)

        log.inf(f"Written SPDX 3.0 JSON-LD to {output_path}")

    def _serialize_document_to_json(self, document: spdx.SpdxDocument, output_path: str):
        """Serialize a single document to plain JSON format."""
        # Collect all elements referenced by this document (same as JSON-LD)
        # document.element contains Element objects, so extract their IDs
        document_element_ids = set()
        if hasattr(document, 'element'):
            for elem in document.element:
                if hasattr(elem, '_id') and elem._id:
                    document_element_ids.add(elem._id)
        
        elements_to_serialize = [document]
        if self.creation_info:
            elements_to_serialize.append(self.creation_info)
        if self.tool:
            elements_to_serialize.append(self.tool)
        
        for elem_id in document_element_ids:
            for elem in self.elements:
                if hasattr(elem, '_id') and elem._id == elem_id:
                    if elem not in elements_to_serialize:
                        elements_to_serialize.append(elem)
                    break
        
        elements_data = []
        for element in elements_to_serialize:
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
