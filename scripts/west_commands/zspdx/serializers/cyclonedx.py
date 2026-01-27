# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
CycloneDX format serializer.

This module provides the CycloneDXSerializer class, which produces
CycloneDX 1.5 documents in either JSON or XML format.
"""

from __future__ import annotations

from typing import TYPE_CHECKING, TextIO

from cyclonedx.model import ExternalReference, ExternalReferenceType
from cyclonedx.model.bom import Bom
from cyclonedx.model.component import Component, ComponentType
from cyclonedx.model.license import LicenseExpression
from cyclonedx.output.json import JsonV1Dot5
from cyclonedx.output.xml import XmlV1Dot5
from packageurl import PackageURL
from west import log

from zspdx.adapters.cyclonedx import CycloneDXAdapter
from zspdx.model.relationship import RelationshipType
from zspdx.serializers.base import Serializer, SerializerConfig

if TYPE_CHECKING:
    from zspdx.model.document import SBOMDocument
    from zspdx.model.package import SBOMPackage


class CycloneDXSerializer(Serializer):
    """
    Serializer for CycloneDX 1.5 format.

    This produces CycloneDX documents in either JSON or XML format,
    using the cyclonedx-python-lib library. The output format is
    controlled via the SerializerConfig.extra_options["format"] parameter.

    Attributes:
        config: SerializerConfig controlling serialization behavior.
        adapter: CycloneDXAdapter for generating format-specific identifiers.

    Example:
        >>> from zspdx.serializers import CycloneDXSerializer, SerializerConfig
        >>> from zspdx.model import SBOMDocument
        >>> from pathlib import Path
        >>>
        >>> # JSON output (default)
        >>> config = SerializerConfig()
        >>> serializer = CycloneDXSerializer(config)
        >>> doc = SBOMDocument(internal_id="my-doc", name="my-project")
        >>> # ... populate document ...
        >>> serializer.serialize_to_file(doc, Path("output.cdx.json"))
        >>>
        >>> # XML output
        >>> config = SerializerConfig(extra_options={"format": "xml"})
        >>> serializer = CycloneDXSerializer(config)
        >>> serializer.serialize_to_file(doc, Path("output.cdx.xml"))
    """

    def __init__(self, config: SerializerConfig | None = None) -> None:
        """
        Initialize the CycloneDX serializer.

        Args:
            config: SerializerConfig controlling serialization behavior.
                The extra_options["format"] can be "json" (default) or "xml".
        """
        super().__init__(config)
        self.adapter = CycloneDXAdapter()
        self._format = (
            self.config.extra_options.get("format", "json")
            if self.config.extra_options
            else "json"
        )

    @property
    def format_name(self) -> str:
        """Return 'CycloneDX-1.5-JSON' or 'CycloneDX-1.5-XML'."""
        return f"CycloneDX-1.5-{self._format.upper()}"

    @property
    def file_extension(self) -> str:
        """Return '.cdx.json' or '.cdx.xml'."""
        return ".cdx.json" if self._format == "json" else ".cdx.xml"

    def serialize(self, document: SBOMDocument, output: TextIO) -> None:
        """
        Serialize document to CycloneDX format.

        Args:
            document: The SBOMDocument to serialize.
            output: Text stream to write the output to.
        """
        log.inf(f"Writing CycloneDX 1.5 {self._format.upper()} document {document.name}")
        bom = self._build_bom(document)

        if self._format == "xml":
            outputter = XmlV1Dot5(bom)
        else:
            outputter = JsonV1Dot5(bom)

        output.write(outputter.output_as_string())

    def _build_bom(self, document: SBOMDocument) -> Bom:
        """
        Build CycloneDX Bom from SBOMDocument.

        Args:
            document: The SBOMDocument to convert.

        Returns:
            CycloneDX Bom object.
        """
        bom = Bom()

        # Build component lookup for dependencies
        component_map: dict[str, Component] = {}

        # Add components (packages -> components)
        for pkg in document.packages.values():
            component = self._build_component(pkg)
            if component:
                bom.components.add(component)
                component_map[pkg.internal_id] = component

        # Add dependencies from relationships
        self._add_dependencies(bom, document, component_map)

        return bom

    def _build_component(self, pkg: SBOMPackage) -> Component | None:
        """
        Convert SBOMPackage to CycloneDX Component.

        Args:
            pkg: The SBOMPackage to convert.

        Returns:
            CycloneDX Component, or None if conversion fails.
        """
        bom_ref = self.adapter.get_bom_ref(pkg)
        component_type = self.adapter.map_purpose_to_component_type(pkg.purpose)

        # Build external references
        ext_refs: list[ExternalReference] = []
        purl: PackageURL | None = None
        cpe: str | None = None

        for ref in pkg.external_references:
            if ref.ref_type == "purl":
                try:
                    purl = PackageURL.from_string(ref.locator)
                except Exception as e:
                    log.wrn(f"Invalid PURL '{ref.locator}': {e}")
            elif ref.ref_type == "cpe23":
                cpe = ref.locator
            else:
                # Other reference types
                ext_refs.append(
                    ExternalReference(
                        type=ExternalReferenceType.OTHER,
                        url=ref.locator,
                    )
                )

        # Add VCS reference if download_url present
        if pkg.download_url:
            ext_refs.append(
                ExternalReference(
                    type=ExternalReferenceType.VCS,
                    url=pkg.download_url,
                )
            )

        # Build licenses list
        licenses: list[LicenseExpression] = []
        if pkg.concluded_license and pkg.concluded_license != "NOASSERTION":
            try:
                licenses.append(LicenseExpression(pkg.concluded_license))
            except Exception as e:
                log.wrn(
                    f"Invalid license expression '{pkg.concluded_license}' for "
                    f"package {pkg.name}: {e}"
                )

        # Build version string
        version = pkg.version or pkg.revision or None

        # Create component
        try:
            component = Component(
                type=component_type,
                name=pkg.name,
                version=version,
                bom_ref=bom_ref,
                purl=purl,
                cpe=cpe,
                licenses=licenses if licenses else None,
                external_references=ext_refs if ext_refs else None,
            )
            return component
        except Exception as e:
            log.wrn(f"Failed to create CycloneDX component for {pkg.name}: {e}")
            return None

    def _add_dependencies(
        self,
        bom: Bom,
        document: SBOMDocument,
        component_map: dict[str, Component],
    ) -> None:
        """
        Add dependencies from relationships.

        Converts SBOM relationships to CycloneDX dependency format.
        Only dependency-like relationships are converted:
        - DEPENDS_ON, HAS_PREREQUISITE
        - STATIC_LINK, DYNAMIC_LINK

        Args:
            bom: The CycloneDX Bom to add dependencies to.
            document: The source SBOMDocument.
            component_map: Mapping of internal_id to Component.
        """
        # Collect dependencies for each package
        for pkg in document.packages.values():
            component = component_map.get(pkg.internal_id)
            if not component:
                continue

            deps: list[Component] = []
            for rln in pkg.relationships:
                # Only convert dependency-like relationships
                if rln.relationship_type in (
                    RelationshipType.DEPENDS_ON,
                    RelationshipType.HAS_PREREQUISITE,
                    RelationshipType.STATIC_LINK,
                    RelationshipType.DYNAMIC_LINK,
                ):
                    if rln.target and rln.target.internal_id in component_map:
                        deps.append(component_map[rln.target.internal_id])

            if deps:
                bom.register_dependency(component, deps)
