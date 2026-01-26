# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
CycloneDX serializer for the format-agnostic SBOM model.

This serializer converts SBOMData to CycloneDX 1.5/1.6 format using the
official cyclonedx-python-lib library.
"""

import os
import re
from datetime import datetime, timezone
from typing import Dict, List, Optional, Set
from uuid import uuid4

from cyclonedx.model import ExternalReference, ExternalReferenceType, HashType, XsUri
from cyclonedx.model.bom import Bom
from cyclonedx.model.component import Component, ComponentType
from cyclonedx.model.contact import OrganizationalEntity
from cyclonedx.model.dependency import Dependency
from cyclonedx.model.license import DisjunctiveLicense, LicenseExpression
from cyclonedx.output import make_outputter, OutputFormat
from cyclonedx.schema import SchemaVersion
from cyclonedx.validation import make_schemabased_validator
from west import log

from zspdx.model import ComponentPurpose, SBOMComponent, SBOMData, SBOMFile

# Regex patterns for external reference detection
CPE23TYPE_REGEX = (
    r'^cpe:2\.3:[aho\*\-](:(((\?*|\*?)([a-zA-Z0-9\-\._]|(\\[\\\*\?!"#$$%&\'\(\)\+,\/:;<=>@\[\]\^'
    r"`\{\|}~]))+(\?*|\*?))|[\*\-])){5}(:(([a-zA-Z]{2,3}(-([a-zA-Z]{2}|[0-9]{3}))?)|[\*\-]))(:(((\?*"
    r'|\*?)([a-zA-Z0-9\-\._]|(\\[\\\*\?!"#$$%&\'\(\)\+,\/:;<=>@\[\]\^`\{\|}~]))+(\?*|\*?))|[\*\-])){4}$'
)
PURL_REGEX = r"^pkg:.+(\/.+)?\/.+(@.+)?(\?.+)?(#.+)?$"


class CycloneDXSerializer:
    """Serializer that converts SBOMData to CycloneDX format (JSON and XML)."""

    # Map ComponentPurpose to CycloneDX ComponentType
    PURPOSE_MAP = {
        ComponentPurpose.APPLICATION: ComponentType.APPLICATION,
        ComponentPurpose.LIBRARY: ComponentType.LIBRARY,
        ComponentPurpose.SOURCE: ComponentType.FILE,  # CycloneDX doesn't have SOURCE type
        ComponentPurpose.FILE: ComponentType.FILE,
        ComponentPurpose.FRAMEWORK: ComponentType.FRAMEWORK,
        ComponentPurpose.CONTAINER: ComponentType.CONTAINER,
        ComponentPurpose.FIRMWARE: ComponentType.FIRMWARE,
        ComponentPurpose.DEVICE: ComponentType.DEVICE,
        ComponentPurpose.OPERATING_SYSTEM: ComponentType.OPERATING_SYSTEM,
        ComponentPurpose.DATA: ComponentType.DATA,
        ComponentPurpose.OTHER: ComponentType.LIBRARY,  # Default to library
    }

    # Map hash algorithm names to CycloneDX HashType
    HASH_TYPE_MAP = {
        "SHA1": HashType.SHA_1,
        "SHA256": HashType.SHA_256,
        "SHA384": HashType.SHA_384,
        "SHA512": HashType.SHA_512,
        "MD5": HashType.MD5,
    }

    def __init__(self, sbom_data: SBOMData, schema_version: SchemaVersion = SchemaVersion.V1_5):
        """
        Initialize the CycloneDX serializer.

        Args:
            sbom_data: The format-agnostic SBOM data to serialize.
            schema_version: CycloneDX schema version (default: 1.5).
        """
        self.sbom_data = sbom_data
        self.schema_version = schema_version

        # Track created components for dependency resolution
        self.component_map: Dict[str, Component] = {}
        self.file_component_map: Dict[str, Component] = {}

        # Build information from metadata
        self.build_info = sbom_data.metadata.get('build_info', {}) if sbom_data.metadata else {}

    def _purpose_to_component_type(self, purpose: ComponentPurpose) -> ComponentType:
        """Convert ComponentPurpose enum to CycloneDX ComponentType."""
        return self.PURPOSE_MAP.get(purpose, ComponentType.LIBRARY)

    def _create_component_from_sbom(self, sbom_component: SBOMComponent) -> Component:
        """Convert SBOMComponent to CycloneDX Component."""
        # Determine component type
        comp_type = self._purpose_to_component_type(sbom_component.purpose)

        # Create base component
        component = Component(
            type=comp_type,
            name=sbom_component.name,
            version=sbom_component.version or sbom_component.revision or None,
        )

        # Generate BOM reference (unique identifier within the BOM)
        component.bom_ref = sbom_component.name

        # Add supplier if available
        if sbom_component.supplier:
            component.supplier = OrganizationalEntity(name=sbom_component.supplier)

        # Add licenses
        if sbom_component.concluded_license and sbom_component.concluded_license != "NOASSERTION":
            try:
                # Try to parse as SPDX expression
                component.licenses.add(LicenseExpression(value=sbom_component.concluded_license))
            except Exception:
                # Fall back to simple license
                component.licenses.add(DisjunctiveLicense(name=sbom_component.concluded_license))

        # Add copyright
        if sbom_component.copyright_text and sbom_component.copyright_text != "NOASSERTION":
            # CycloneDX stores copyright in the component's copyright field
            component.copyright = sbom_component.copyright_text

        # Add external references (CPE, PURL, URL)
        for ref in sbom_component.external_references:
            if not ref:
                continue

            if re.fullmatch(CPE23TYPE_REGEX, ref):
                # CPE reference - set as cpe field
                component.cpe = ref
            elif re.fullmatch(PURL_REGEX, ref):
                # PURL reference - set as purl field
                try:
                    from packageurl import PackageURL
                    component.purl = PackageURL.from_string(ref)
                except Exception as e:
                    log.wrn(f"Failed to parse PURL {ref}: {e}")
            else:
                # Other reference - add as external reference
                try:
                    ext_ref = ExternalReference(
                        type=ExternalReferenceType.OTHER,
                        url=XsUri(ref)
                    )
                    component.external_references.add(ext_ref)
                except Exception as e:
                    log.wrn(f"Failed to add external reference {ref}: {e}")

        # Add repository URL as VCS reference
        if sbom_component.url:
            try:
                vcs_ref = ExternalReference(
                    type=ExternalReferenceType.VCS,
                    url=XsUri(sbom_component.url)
                )
                component.external_references.add(vcs_ref)
            except Exception as e:
                log.wrn(f"Failed to add VCS reference {sbom_component.url}: {e}")

        # Add hashes from files (aggregate)
        # CycloneDX components can have hashes; we use the verification code concept
        # by adding hashes from the primary build file if available
        if sbom_component.target_build_file:
            self._add_hashes_to_component(component, sbom_component.target_build_file)

        return component

    def _add_hashes_to_component(self, component: Component, sbom_file: SBOMFile) -> None:
        """Add file hashes to a CycloneDX component."""
        from cyclonedx.model import HashAlgorithm

        for algo_name, hash_value in sbom_file.hashes.items():
            if not hash_value:
                continue

            hash_type = self.HASH_TYPE_MAP.get(algo_name)
            if hash_type:
                try:
                    from cyclonedx.model import Hash
                    component.hashes.add(Hash(alg=hash_type, content=hash_value))
                except Exception as e:
                    log.wrn(f"Failed to add hash {algo_name}: {e}")

    def _create_file_component(self, sbom_file: SBOMFile) -> Component:
        """Convert SBOMFile to CycloneDX Component (type=FILE)."""
        # Use relative path as name, or filename if not available
        name = sbom_file.relative_path or os.path.basename(sbom_file.path)

        component = Component(
            type=ComponentType.FILE,
            name=name,
        )

        # Generate unique BOM reference
        component.bom_ref = f"file-{name.replace('/', '-').replace('.', '-')}"

        # Add hashes
        self._add_hashes_to_component(component, sbom_file)

        # Add license if available
        if sbom_file.concluded_license and sbom_file.concluded_license != "NOASSERTION":
            try:
                component.licenses.add(LicenseExpression(value=sbom_file.concluded_license))
            except Exception:
                component.licenses.add(DisjunctiveLicense(name=sbom_file.concluded_license))

        # Add copyright
        if sbom_file.copyright_text and sbom_file.copyright_text != "NOASSERTION":
            component.copyright = sbom_file.copyright_text

        return component

    def _create_dependencies(self, bom: Bom) -> None:
        """Create CycloneDX dependencies from SBOM relationships."""
        # Build dependency map: component -> set of dependencies
        dependency_map: Dict[str, Set[str]] = {}

        # Process component relationships
        for sbom_component in self.sbom_data.components.values():
            comp_ref = sbom_component.name

            for rel in sbom_component.relationships:
                rel_type = rel.relationship_type

                # Map relationship types to dependencies
                if rel_type in ("HAS_PREREQUISITE", "DEPENDS_ON", "STATIC_LINK", "DYNAMIC_LINK"):
                    if comp_ref not in dependency_map:
                        dependency_map[comp_ref] = set()

                    for to_elem in rel.to_elements:
                        if isinstance(to_elem, SBOMComponent):
                            dependency_map[comp_ref].add(to_elem.name)
                        elif isinstance(to_elem, str):
                            dependency_map[comp_ref].add(to_elem)

        # Create CycloneDX Dependency objects
        for comp_ref, deps in dependency_map.items():
            if comp_ref in self.component_map:
                component = self.component_map[comp_ref]
                dep_components = []

                for dep_ref in deps:
                    if dep_ref in self.component_map:
                        dep_components.append(self.component_map[dep_ref])

                if dep_components:
                    dependency = Dependency(ref=component.bom_ref)
                    for dep_comp in dep_components:
                        dependency.dependencies.add(Dependency(ref=dep_comp.bom_ref))
                    bom.dependencies.add(dependency)

    def _create_bom(self) -> Bom:
        """Create a CycloneDX BOM from the SBOM data."""
        # Create the BOM
        bom = Bom()

        # Set BOM metadata
        bom.serial_number = uuid4()
        bom.version = 1

        # Add tool metadata
        from cyclonedx.model.bom import BomMetaData
        from cyclonedx.model.tool import Tool

        metadata = BomMetaData()
        metadata.timestamp = datetime.now(timezone.utc)

        # Add creation tool
        tool = Tool(name="Zephyr SPDX/CycloneDX builder")
        metadata.tools.add(tool)

        # Add build tools if available
        if self.build_info:
            if self.build_info.get("cmake_version"):
                cmake_tool = Tool(
                    name="CMake",
                    version=self.build_info.get("cmake_version")
                )
                metadata.tools.add(cmake_tool)

            compiler_path = self.build_info.get("cmake_compiler", "")
            if compiler_path:
                compiler_name = os.path.basename(compiler_path)
                compiler_tool = Tool(name=compiler_name)
                metadata.tools.add(compiler_tool)

        bom.metadata = metadata

        # Create components from SBOM components
        for sbom_component in self.sbom_data.components.values():
            component = self._create_component_from_sbom(sbom_component)
            self.component_map[sbom_component.name] = component
            bom.components.add(component)

            # Optionally add files as sub-components
            # Note: CycloneDX 1.5+ supports nested components
            # For now, we skip file-level components to keep the BOM manageable
            # Files can be added via the component's evidence or as separate components

        # Create dependencies
        self._create_dependencies(bom)

        return bom

    def serialize(self, output_dir: str) -> bool:
        """
        Serialize SBOMData to CycloneDX format (JSON and XML).

        Args:
            output_dir: Directory to write output files to.

        Returns:
            True if serialization succeeded, False otherwise.
        """
        try:
            # Validate input
            if not self.sbom_data:
                log.err("SBOMData is None or empty")
                return False

            if not os.path.exists(output_dir):
                log.err(f"Output directory does not exist: {output_dir}")
                return False

            # Create the BOM
            bom = self._create_bom()

            # Serialize to JSON
            json_path = os.path.join(output_dir, "sbom.cdx.json")
            self._write_bom(bom, json_path, OutputFormat.JSON)

            # Serialize to XML
            xml_path = os.path.join(output_dir, "sbom.cdx.xml")
            self._write_bom(bom, xml_path, OutputFormat.XML)

            log.inf(f"CycloneDX documents written to {output_dir}")
            return True

        except Exception as e:
            log.err(f"Failed to serialize CycloneDX document: {e}")
            import traceback
            log.dbg(traceback.format_exc())
            return False

    def _write_bom(self, bom: Bom, output_path: str, output_format: OutputFormat) -> None:
        """Write BOM to file in specified format."""
        outputter = make_outputter(bom, output_format, self.schema_version)
        serialized = outputter.output_as_string()

        with open(output_path, "w", encoding="utf-8") as f:
            f.write(serialized)

        log.inf(f"Written CycloneDX {output_format.name} to {output_path}")

        # Optionally validate the output
        try:
            validator = make_schemabased_validator(output_format, self.schema_version)
            validation_error = validator.validate_str(serialized)
            if validation_error:
                log.wrn(f"CycloneDX validation warning for {output_path}: {validation_error}")
        except Exception as e:
            log.dbg(f"Could not validate CycloneDX output: {e}")
