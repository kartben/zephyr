# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

import hashlib
import logging
import os
import re
from datetime import UTC, datetime

from spdx_python_model import v3_0_1 as spdx

from zspdx.adequacy import VERDICT_HELP, requirement_adequacy
from zspdx.model import (
    NOASSERTION,
    ComponentPurpose,
    ExternalReferenceType,
    RelationshipType,
    SBOMComponent,
    SBOMDocument,
    SBOMFile,
    SBOMGraph,
    SBOMRelationship,
)
from zspdx.serializers.helpers import (
    CPE23TYPE_REGEX,
    PURL_REGEX,
    generate_download_url,
    get_standard_licenses,
    normalize_spdx_name,
)
from zspdx.sources import content_byte_range
from zspdx.spdxids import get_unique_file_id
from zspdx.twister import verdict_from_rollup
from zspdx.version import SPDX_VERSION_3_1

_logger = logging.getLogger(__name__)


class SPDX3Serializer:
    """Serializer that converts SBOMGraph to SPDX 3.0 format (JSON-LD)."""

    # Human-readable document name per SBOMDocument.
    _DOCUMENT_NAMES = {
        "app": "Zephyr Application",
        "zephyr": "Zephyr RTOS",
        "build": "Zephyr Build Artifacts",
        "modules-deps": "Zephyr Module Dependencies",
        "sdk": "Zephyr SDK",
    }

    # Name of the SBOMDocument that hosts the Build profile (build targets).
    _BUILD_DOCUMENT = "build"

    # build_buildType is mandatory and must be a URI; used when the SBOMBuild
    # does not provide an absolute URI of its own.
    _DEFAULT_BUILD_TYPE = "urn:spdx.dev:zephyr-cmake"

    def __init__(self, sbom_graph: SBOMGraph, spdx_version=None):
        self.sbom_data = sbom_graph
        self.spdx_version = spdx_version

        # Select the SPDX 3.x Python bindings matching the requested spec version.
        # v3.1 is a superset of v3.0.1 with identical public class names, so the code
        # below stays version-agnostic; only the binding module (which drives the
        # emitted JSON-LD @context and specVersion) and the availability of the newer
        # Requirement / FunctionalSafety classes differ. The module-global ``spdx`` is
        # rebound so every method picks up the selected binding.
        self.is_31 = spdx_version is not None and spdx_version >= SPDX_VERSION_3_1
        global spdx
        if self.is_31:
            from spdx_python_model import v3_1 as _binding

            self.spec_version = "3.1.0"
        else:
            from spdx_python_model import v3_0_1 as _binding

            self.spec_version = "3.0.1"
        spdx = _binding

        # Track SPDX 3.0 elements
        self.elements = []  # All SPDX3 elements (packages, files, relationships, etc.)
        self.component_elements = {}  # component_name -> software_Package
        self.file_elements = {}  # file_path -> software_File
        self.relationship_elements = []  # List of Relationship objects

        # Track original from_id for relationships (before reversal)
        # This is used to assign cross-document relationships to the correct document
        # Key: relationship._id, Value: original from_id
        self.relationship_original_from = {}

        # Shared objects
        self.tool = None  # Tool element for createdUsing
        self.creator_agent = None  # SoftwareAgent for createdBy
        self.creation_info = None
        self.documents = {}  # doc_name -> SpdxDocument

        # SPDX 3.0 Build profile state
        self.build = None  # overall build_Build element
        self.target_builds = {}  # component name -> per-target build_Build element
        self.build_tools = {}  # tool key -> Tool element
        self.build_info = sbom_graph.build.metadata if sbom_graph.build else {}

        # Track file IDs for uniqueness
        self.filename_counts = {}

        # Lazily built map: element id -> name of the document that defines it.
        # Used to reference cross-document elements via ExternalMap instead of
        # re-serializing them in every document that mentions them.
        self._element_home = None

        # SPDX 3.1 FunctionalSafety state, populated by _create_functional_safety().
        self.requirement_elements = {}  # requirement UID -> Requirement
        self.specification_elements = {}  # design UID -> Specification
        self.snippet_elements = {}  # (path, start, end) -> software_Snippet
        self.synth_file_elements = {}  # abs path -> safety-doc-owned software_File
        # Every element that belongs in the standalone "safety" document.
        self.fs_elements = []
        # Source-File ids referenced by snippets but defined in another document;
        # declared via ExternalMap in the safety document.
        self.fs_import_ids = set()

        # Namespace prefixes for shortened IDs
        self.namespace_prefixes = {}
        if self.sbom_data.namespace_prefix:
            prefix = "zephyr"
            uri = self.sbom_data.namespace_prefix.rstrip("/") + "/"
            self.namespace_prefixes[uri] = prefix

    def _shorten_id(self, full_uri: str) -> str:
        """Shorten a URI-based ID using known namespace prefixes."""
        for ns_uri, prefix in self.namespace_prefixes.items():
            if full_uri.startswith(ns_uri):
                return full_uri.replace(ns_uri, f"{prefix}:", 1)
        return full_uri

    def _generate_package_id(self, component_name: str) -> str:
        """Generate URI-based ID for a package."""
        normalized = normalize_spdx_name(component_name)
        namespace = self.sbom_data.namespace_prefix.rstrip("/")
        full_uri = f"{namespace}/packages/{normalized}"
        return self._shorten_id(full_uri)

    def _generate_file_id(self, file_path: str) -> str:
        """Generate URI-based ID for a file."""
        filename_only = os.path.basename(file_path)
        unique_id = get_unique_file_id(filename_only, self.filename_counts)
        # Remove "SPDXRef-" prefix and normalize
        normalized_id = unique_id.replace("SPDXRef-", "").replace("_", "-")
        namespace = self.sbom_data.namespace_prefix.rstrip("/")
        full_uri = f"{namespace}/files/{normalized_id}"
        return self._shorten_id(full_uri)

    def _generate_relationship_id(self, index: int) -> str:
        """Generate URI-based ID for a relationship."""
        namespace = self.sbom_data.namespace_prefix.rstrip("/")
        full_uri = f"{namespace}/relationships/{index}"
        return self._shorten_id(full_uri)

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
        """Initialize shared Tool and CreationInfo objects.

        Per SPDX 3.0.1 spec:
        - CreationInfo.createdBy takes Agent (who created the SPDX data) - REQUIRED
        - CreationInfo.createdUsing takes Tool (what tool created the SPDX data) - optional
        """
        namespace = self.sbom_data.namespace_prefix.rstrip("/")

        # Create a SoftwareAgent representing the automated SBOM generation process
        # This satisfies the required createdBy field
        if self.tool is None:
            # Create the creator agent (required by createdBy)
            self.creator_agent = spdx.SoftwareAgent()
            self.creator_agent._id = self._shorten_id(f"{namespace}/agents/west-spdx-agent")
            self.creator_agent.name = "West SPDX Generator"
            self.elements.append(self.creator_agent)

            # Create the tool (for createdUsing)
            self.tool = spdx.Tool()
            self.tool._id = self._shorten_id(f"{namespace}/tools/west-spdx")
            self.tool.name = "West SPDX Tool"
            self.elements.append(self.tool)

        if self.creation_info is None:
            self.creation_info = spdx.CreationInfo()
            self.creation_info._id = self._shorten_id(f"{namespace}/creationinfo")
            self.creation_info.created = datetime.now(UTC)
            # createdBy references the Agent that created this SPDX document (REQUIRED)
            self.creation_info.createdBy.append(self.creator_agent._id)
            # createdUsing references the Tool that created this SPDX document (optional)
            self.creation_info.createdUsing.append(self.tool._id)
            self.creation_info.specVersion = self.spec_version
            self.elements.append(self.creation_info)

            # Now set the tool's and agent's creationInfo
            self.tool.creationInfo = self.creation_info._id
            self.creator_agent.creationInfo = self.creation_info._id

        # Build profile elements; no-ops when no build information was collected.
        self._create_build_tools()
        self._create_build_object()

    # ---- SPDX 3.0 Build profile -------------------------------------------------

    def _create_tool(
        self, key: str, name: str, path: str = "", version: str = "", identifiers=None
    ) -> spdx.Tool:
        """Create and register a build Tool element keyed by ``key``.

        Tool identity lives on the Tool itself: ``path`` is recorded as an ``other`` external
        identifier and ``version`` as a ``packageUrl`` (``pkg:generic/<tool>@<version>``), since
        SPDX 3.0 has no Tool version field. Extra ``(ExternalIdentifierType, value)`` tuples may be
        passed in ``identifiers``.
        """
        namespace = self.sbom_data.namespace_prefix.rstrip("/")
        tool = spdx.Tool()
        tool._id = self._shorten_id(f"{namespace}/tools/{key}")
        tool.name = name
        tool.creationInfo = self.creation_info._id

        ext_ids = []
        if path:
            ext_ids.append((spdx.ExternalIdentifierType.other, path))
        if version:
            pkg = os.path.basename(path) if path else key
            ext_ids.append((spdx.ExternalIdentifierType.packageUrl, f"pkg:generic/{pkg}@{version}"))
        ext_ids += identifiers or []

        for id_type, id_value in ext_ids:
            if not id_value:
                continue
            ext_id = spdx.ExternalIdentifier()
            ext_id.externalIdentifierType = id_type
            ext_id.identifier = id_value
            tool.externalIdentifier.append(ext_id)

        self.elements.append(tool)
        self.build_tools[key] = tool
        return tool

    def _create_build_tools(self):
        """Create a Tool element for each build tool we collected information for."""
        if self.build_tools or not self.build_info:
            return

        info = self.build_info
        self._create_cmake_tool(info)

        # the C compiler also records the target architecture
        c_path = info.get("cmake_compiler") or info.get("cmake_c_compiler", "")
        if c_path:
            version = info.get("c_compiler_version", "")
            self._create_tool(
                "c-compiler",
                self._compiler_tool_name("C Compiler", c_path, version),
                path=c_path,
                version=version,
                identifiers=self._compiler_arch_identifiers(info),
            )

        tool_specs = [
            ("cxx-compiler", "C++ Compiler", "cmake_cxx_compiler", "cxx_compiler_version"),
            ("asm-compiler", "Assembler", "cmake_asm_compiler", "asm_compiler_version"),
            ("linker", "Linker", "cmake_linker", "linker_version"),
            ("archiver", "Archiver", "cmake_ar", "ar_version"),
        ]
        for key, label, path_key, version_key in tool_specs:
            path = info.get(path_key, "")
            # skip missing tools and the C++ compiler when it is the same binary as the C compiler
            if not path or (key == "cxx-compiler" and path == c_path):
                continue
            version = info.get(version_key, "")
            self._create_tool(
                key,
                self._compiler_tool_name(label, path, version),
                path=path,
                version=version,
            )

    def _create_cmake_tool(self, info: dict):
        """Create the CMake build Tool from the collected build info, when present."""
        if not (info.get("cmake_version") or info.get("cmake_generator")):
            return
        name = "CMake"
        if info.get("cmake_version"):
            name += f" {info['cmake_version']}"
        if info.get("cmake_generator"):
            name += f" ({info['cmake_generator']})"
        self._create_tool("cmake", name, version=info.get("cmake_version", ""))

    @staticmethod
    def _compiler_arch_identifiers(info: dict):
        """Target-architecture external identifier for a compiler, or ``None`` if unknown."""
        processor = info.get("cmake_system_processor")
        if not processor:
            return None
        return [(spdx.ExternalIdentifierType.other, f"target-arch:{processor}")]

    @staticmethod
    def _compiler_tool_name(label: str, path: str, version: str) -> str:
        """Build a tool display name like ``C Compiler (gcc 12.2.0)``."""
        basename = os.path.basename(path)
        if version:
            return f"{label} ({basename} {version})"
        return f"{label} ({basename})"

    def _create_build_object(self):
        """Create the SPDX 3.0 ``build_Build`` element."""
        if self.build is not None:
            return
        if not self.build_info and not self.sbom_data.build:
            return

        namespace = self.sbom_data.namespace_prefix.rstrip("/")
        self.build = spdx.build_Build()
        self.build._id = self._shorten_id(f"{namespace}/builds/default")
        self.build.creationInfo = self.creation_info._id
        self.build.build_buildType = self._DEFAULT_BUILD_TYPE

        sbom_build = self.sbom_data.build
        if sbom_build:
            if sbom_build.id:
                self.build.build_buildId = sbom_build.id
            if sbom_build.build_type:
                build_type = sbom_build.build_type
                if not build_type.startswith("http"):
                    build_type = f"https://zephyrproject.org/build-types/{build_type}"
                self.build.build_buildType = build_type
            if sbom_build.started_at:
                self.build.build_buildStartTime = sbom_build.started_at
            if sbom_build.finished_at:
                self.build.build_buildEndTime = sbom_build.finished_at

        self._add_build_parameters()
        self._add_build_environment()
        self.elements.append(self.build)

    def _add_build_environment(self):
        """Populate ``build_environment`` from the collected environment variables."""
        if not self.build:
            return
        for key, value in sorted(self.build_info.get("environment", {}).items()):
            if not value:
                continue
            entry = spdx.DictionaryEntry()
            entry.key = key
            entry.value = str(value)
            self.build.build_environment.append(entry)

    def _add_build_parameters(self):
        """Populate ``build_parameter`` with the global build configuration."""
        info = self.build_info
        if not self.build or not info:
            return

        for key, info_key in (
            ("cmake:generator", "cmake_generator"),
            ("cmake:buildType", "cmake_build_type"),
            ("target:system", "cmake_system_name"),
            ("target:processor", "cmake_system_processor"),
        ):
            value = info.get(info_key, "")
            if value:
                entry = spdx.DictionaryEntry()
                entry.key = key
                entry.value = str(value)
                self.build.build_parameter.append(entry)

    @staticmethod
    def _is_build_target_component(component: SBOMComponent) -> bool:
        """Whether a component is a build target (produces an artifact).

        Source aggregation and ``*-deps`` components are inputs, not outputs.
        """
        if component.name in {"app-sources", "zephyr-sources", "sdk-sources"}:
            return False
        return not component.name.endswith("-deps")

    def _new_build_relationship(
        self,
        rel_type: spdx.RelationshipType,
        from_id: str,
        to_ids: list[str],
        description: str = "",
    ) -> spdx.LifecycleScopedRelationship:
        """Create and register a relationship scoped to the ``build`` lifecycle."""
        rel = spdx.LifecycleScopedRelationship()
        rel._id = self._generate_relationship_id(len(self.relationship_elements))
        rel.relationshipType = rel_type
        rel.from_ = from_id
        rel.to = list(to_ids)
        rel.scope = spdx.LifecycleScopeType.build
        rel.creationInfo = self.creation_info._id
        if description:
            rel.description = description
        self.elements.append(rel)
        self.relationship_elements.append(rel)
        return rel

    def _create_build_relationships(self):
        """Create the Build profile's build-scoped relationships.

        The overall build ``usesTool`` every build tool, ``hasInput`` the source/dependency packages
        and ``hasOutput`` the final image(s). Each other build target gets its own sub-build
        recording the exact sources and tools that produced its artifact. Every relationship's
        ``from`` is a Build, so they all stay in the build document.
        """
        if not self.build:
            return

        for tool in self.build_tools.values():
            self._new_build_relationship(spdx.RelationshipType.usesTool, self.build._id, [tool._id])

        self._create_build_output_relationships()

        input_ids = self._build_input_ids()
        if input_ids:
            self._new_build_relationship(spdx.RelationshipType.hasInput, self.build._id, input_ids)

    def _create_build_output_relationships(self):
        """Emit ``hasOutput`` for the final image(s) and a sub-build for every other target."""
        for component in self.sbom_data.components.values():
            if not self._is_build_target_component(component):
                continue
            if not component.target_build_file:
                continue
            build_file = self.file_elements.get(component.target_build_file.path)
            if not build_file:
                continue
            if component.metadata.get("target_type") == "EXECUTABLE":
                # final images are outputs of the overall build; their compiled
                # sources/headers are inputs of that same build
                self._new_build_relationship(
                    spdx.RelationshipType.hasOutput, self.build._id, [build_file._id]
                )
                input_ids = self._artifact_input_ids(component)
                if input_ids:
                    self._new_build_relationship(
                        spdx.RelationshipType.hasInput, self.build._id, input_ids
                    )
            else:
                self._create_target_build(component, build_file)

    def _artifact_input_ids(self, component: SBOMComponent) -> list[str]:
        """Element IDs of the files that fed a target's artifact.

        These are the artifact's ``GENERATED_FROM`` endpoints: the compiled sources and, when
        include analysis is enabled, the headers they pulled in. They become the Build's
        ``hasInput`` for the target.
        """
        input_ids = []
        for rel in component.target_build_file.relationships:
            if rel.relationship_type != RelationshipType.GENERATED_FROM:
                continue
            for source in rel.to_elements:
                source_id = self._get_element_id(source)
                if source_id and source_id not in input_ids:
                    input_ids.append(source_id)
        return input_ids

    def _create_target_build(self, component: SBOMComponent, build_file) -> None:
        """Create a per-target sub-build for one artifact-producing target.

        The sub-build ``hasOutput`` the target's artifact, ``hasInput`` its compiled sources/headers
        and ``usesTool`` the specific compiler(s) and archiver/linker that produced it. Sub-builds
        connect to the overall build through the artifacts they share, which carry ``hasStaticLink``
        edges to the final image.
        """
        namespace = self.sbom_data.namespace_prefix.rstrip("/")
        target_build = spdx.build_Build()
        target_build._id = self._shorten_id(
            f"{namespace}/builds/{normalize_spdx_name(component.name)}"
        )
        target_build.creationInfo = self.creation_info._id
        target_build.build_buildType = self.build.build_buildType
        # per-language compile flags and defines used to produce this artifact
        for kind in ("flags", "defines"):
            for lang, value in sorted(component.metadata.get(f"compile_{kind}", {}).items()):
                entry = spdx.DictionaryEntry()
                entry.key = f"compile:{kind}:{lang}"
                entry.value = value
                target_build.build_parameter.append(entry)
        self.elements.append(target_build)
        self.target_builds[component.name] = target_build

        self._new_build_relationship(
            spdx.RelationshipType.hasOutput, target_build._id, [build_file._id]
        )

        # the target's compiled sources/headers are the artifact's GENERATED_FROM endpoints
        input_ids = self._artifact_input_ids(component)
        if input_ids:
            self._new_build_relationship(
                spdx.RelationshipType.hasInput, target_build._id, input_ids
            )

        for tool_id in self._target_tool_ids(component):
            self._new_build_relationship(
                spdx.RelationshipType.usesTool, target_build._id, [tool_id]
            )

    def _target_tool_ids(self, component: SBOMComponent) -> list[str]:
        """Tool IDs that built ``component``: compiler(s) per language plus the archiver (static
        libraries) or linker (shared/module libraries).
        """
        language_to_tool = {
            "C": "c-compiler",
            "CXX": "cxx-compiler",
            "ASM": "asm-compiler",
            "ASM-ATT": "asm-compiler",
        }
        tool_ids = []
        for lang in component.metadata.get("compile_languages", []):
            tool = self.build_tools.get(language_to_tool.get(lang, ""))
            if tool and tool._id not in tool_ids:
                tool_ids.append(tool._id)

        target_type = component.metadata.get("target_type", "")
        if target_type == "STATIC_LIBRARY":
            extra = self.build_tools.get("archiver")
        elif target_type in ("SHARED_LIBRARY", "MODULE_LIBRARY"):
            extra = self.build_tools.get("linker")
        else:
            extra = None
        if extra and extra._id not in tool_ids:
            tool_ids.append(extra._id)
        return tool_ids

    def _build_input_ids(self) -> list[str]:
        """Package IDs of the build's inputs (the source/dependency roots).

        The described (root) components of every non-build document, falling back to all of a
        document's components when it declares no root.
        """
        input_ids = []
        seen = set()
        for sbom_doc in self.sbom_data.documents.values():
            if sbom_doc.name == self._BUILD_DOCUMENT:
                continue
            names = sbom_doc.described_components or list(sbom_doc.components.keys())
            for name in names:
                package = self.component_elements.get(name)
                if package and package._id not in seen:
                    seen.add(package._id)
                    input_ids.append(package._id)
        return input_ids

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
            package.software_downloadLocation = generate_download_url(
                component.url, component.revision
            )
        else:
            package.software_downloadLocation = NOASSERTION

        # Copyright
        package.software_copyrightText = component.copyright_text or NOASSERTION

        # License information will be added via relationships after package creation

        # External references (CPE, PURL)
        for ref in component.external_references:
            if not ref or not ref.locator:
                _logger.warning(f"Invalid external reference: {ref}")
                continue
            if ref.reference_type == ExternalReferenceType.CPE23 and re.fullmatch(
                CPE23TYPE_REGEX, ref.locator
            ):
                ext_id = spdx.ExternalIdentifier()
                ext_id.externalIdentifierType = spdx.ExternalIdentifierType.cpe23
                ext_id.identifier = ref.locator
                package.externalIdentifier.append(ext_id)
            elif ref.reference_type == ExternalReferenceType.PURL and re.fullmatch(
                PURL_REGEX, ref.locator
            ):
                ext_id = spdx.ExternalIdentifier()
                ext_id.externalIdentifierType = spdx.ExternalIdentifierType.packageUrl
                ext_id.identifier = ref.locator
                package.externalIdentifier.append(ext_id)
            else:
                _logger.warning(f"Unknown external reference format: {ref.locator}")

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
        file_element.software_copyrightText = file_obj.copyright_text or NOASSERTION

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
                    _logger.warning(f"Unknown hash algorithm: {hash_type}")
                    continue
                hash_obj.hashValue = hash_value
                file_element.verifiedUsing.append(hash_obj)

        # License information will be added via relationships after file creation

        self.elements.append(file_element)
        self.file_elements[file_obj.path] = file_element
        return file_element

    def _map_relationship_type(self, rel_type: str) -> tuple[spdx.RelationshipType, bool]:
        """Map relationship type string to SPDX 3.0 RelationshipType enum.
        Returns a tuple of (RelationshipType, reversed).
        """
        # Map SPDX 2.x relationship types to SPDX 3.0 RelationshipType.
        # GENERATED_FROM is intentionally absent: it is handled by the Build profile
        # (see _create_relationships and _artifact_input_ids).
        type_map = {
            "HAS_PREREQUISITE": (spdx.RelationshipType.dependsOn, False),
            "STATIC_LINK": (spdx.RelationshipType.hasStaticLink, False),
            "CONTAINS": (spdx.RelationshipType.contains, False),
            "DESCRIBES": (spdx.RelationshipType.describes, False),
            "DEPENDS_ON": (spdx.RelationshipType.dependsOn, False),
            "DYNAMIC_LINK": (spdx.RelationshipType.hasDynamicLink, False),
            "BUILD_TOOL_OF": (spdx.RelationshipType.usesTool, True),
            "DEV_TOOL_OF": (spdx.RelationshipType.usesTool, True),
            "TEST_TOOL_OF": (spdx.RelationshipType.usesTool, True),
            "OTHER": (spdx.RelationshipType.other, False),
        }
        return type_map.get(rel_type, (spdx.RelationshipType.other, False))

    def _get_element_id(self, element):
        """Get SPDX 3.0 element ID from various element types."""
        if isinstance(element, SBOMComponent):
            if element.name in self.component_elements:
                return self.component_elements[element.name]._id
        elif isinstance(element, SBOMFile):
            if element.path in self.file_elements:
                return self.file_elements[element.path]._id
        elif isinstance(element, str):
            if element == NOASSERTION:
                return spdx.IndividualElement.NoAssertionElement
            # Try to resolve as component name first, then file path
            if element in self.component_elements:
                return self.component_elements[element]._id
            elif element in self.file_elements:
                return self.file_elements[element]._id
        return None

    def _create_relationship(self, rel: SBOMRelationship) -> spdx.Relationship | None:
        """Convert SBOMRelationship to SPDX 3.0 Relationship."""
        # Get from_element ID
        from_id = self._get_element_id(rel.from_element)
        if not from_id:
            _logger.warning(
                f"Could not resolve from_element for relationship: {rel.relationship_type}"
            )
            return None

        # Get to_elements IDs
        to_elements = rel.to_elements if isinstance(rel.to_elements, list) else [rel.to_elements]
        to_ids = []
        for to_elem in to_elements:
            to_id = self._get_element_id(to_elem)
            if to_id:
                to_ids.append(to_id)
            else:
                _logger.warning(
                    f"Could not resolve to_element for relationship: {rel.relationship_type}"
                )

        if not to_ids:
            _logger.warning(f"No valid to_elements found for relationship: {rel.relationship_type}")
            return None

        rel_type, is_reversed = self._map_relationship_type(rel.relationship_type)

        relationship = spdx.Relationship()
        relationship._id = self._generate_relationship_id(len(self.relationship_elements))
        relationship.relationshipType = rel_type
        if is_reversed:
            # Swap from/to for reversed relationships (e.g., A GENERATED_FROM B -> B generates A)
            if len(to_ids) > 1:
                _logger.warning(
                    f"Reversed relationship {rel.relationship_type} with multiple to_elements "
                    "is not well-defined for swapping. Using first element."
                )
            relationship.from_ = to_ids[0]
            relationship.to = [from_id]
        else:
            relationship.from_ = from_id
            relationship.to = to_ids
        relationship.creationInfo = self.creation_info._id

        # Store original from_id for document assignment (before reversal)
        # This ensures cross-document relationships are placed in the document
        # of the original "from" element, matching SPDX 2.x behavior
        self.relationship_original_from[relationship._id] = from_id

        self.elements.append(relationship)
        self.relationship_elements.append(relationship)
        return relationship

    def _create_license_expression(
        self, license_str: str
    ) -> spdx.simplelicensing_LicenseExpression | None:
        """Create a license expression object and add it to elements."""
        if not license_str or license_str == NOASSERTION:
            return None

        license_expr = spdx.simplelicensing_LicenseExpression()
        standard_licenses = get_standard_licenses()

        # Check if it's a standard license ID
        if license_str in standard_licenses:
            license_expr._id = f"https://spdx.org/licenses/{license_str}"
        else:
            # Custom license - use a namespace-based ID
            namespace = self.sbom_data.namespace_prefix.rstrip("/")
            # Normalize the license string for use in URI
            normalized = normalize_spdx_name(
                license_str.replace(" ", "-").replace("(", "").replace(")", "")
            )
            license_expr._id = self._shorten_id(f"{namespace}/licenses/{normalized}")

        license_expr.simplelicensing_licenseExpression = license_str
        license_expr.creationInfo = self.creation_info._id

        # Add to elements if not already present
        existing_ids = {elem._id for elem in self.elements if hasattr(elem, '_id')}
        if license_expr._id not in existing_ids:
            self.elements.append(license_expr)

        return license_expr

    def _get_license_target_id(self, license_str: str) -> str | None:
        """Return the SPDX 3 target ID for a license value."""
        if not license_str or license_str == NOASSERTION:
            return spdx.expandedlicensing_IndividualLicensingInfo.NoAssertionLicense

        license_expr = self._create_license_expression(license_str)
        return license_expr._id if license_expr else None

    def _create_license_relationship(
        self,
        from_id: str,
        license_str: str,
        relationship_type: spdx.RelationshipType,
    ) -> spdx.Relationship | None:
        """Create a relationship from an element to a license target.

        A NOASSERTION license still produces an explicit relationship to NoAssertionLicense rather
        than being skipped: per the SPDX 3.0 Licensing profile a "known unknown"
        (NoAssertionLicense) is deliberately distinct from a missing relationship, and this mirrors
        what the SPDX 2.x output records (``LicenseConcluded: NOASSERTION``).
        """
        license_id = self._get_license_target_id(license_str)
        if not license_id:
            return None

        rel = spdx.Relationship()
        rel._id = self._generate_relationship_id(len(self.relationship_elements))
        rel.relationshipType = relationship_type
        rel.from_ = from_id
        rel.to = [license_id]
        rel.creationInfo = self.creation_info._id
        self.elements.append(rel)
        self.relationship_elements.append(rel)
        return rel

    def _document_owned_elements(self, sbom_doc: SBOMDocument):
        """Yield the package and file elements defined by ``sbom_doc``."""
        for component in sbom_doc.components.values():
            package = self.component_elements.get(component.name)
            if package:
                yield package
            for file_obj in component.files.values():
                file_element = self.file_elements.get(file_obj.path)
                if file_element:
                    yield file_element

    def _build_owned_elements(self):
        """Yield the Build-profile elements, all defined in the build document."""
        if self.build:
            yield self.build
        yield from self.target_builds.values()
        yield from self.build_tools.values()

    def _element_home_index(self) -> dict:
        """Map each package/file element id to the document that defines it.

        Every component belongs to exactly one document and every file to exactly
        one component, so this "home" is unique. Build-profile elements live in the
        build document. Ids absent from the map (licenses, the tool/agent, well-known
        individuals) have no single owner and are serialized wherever referenced.
        """
        if self._element_home is None:
            home = {}
            for doc_name, sbom_doc in self.sbom_data.documents.items():
                for element in self._document_owned_elements(sbom_doc):
                    home[element._id] = doc_name
            for element in self._build_owned_elements():
                home[element._id] = self._BUILD_DOCUMENT
            self._element_home = home
        return self._element_home

    def _create_document(self, sbom_doc: SBOMDocument) -> spdx.SpdxDocument:
        """Create an SPDX 3.0 document for a specific SBOMDocument."""
        self._initialize_shared_objects()

        document = self._new_spdx_document(sbom_doc)
        components = sbom_doc.components.values()

        element_ids, import_ids = self._collect_document_element_ids(sbom_doc, components)
        self._populate_document(document, element_ids, import_ids, components, sbom_doc)

        self.elements.append(document)
        self.documents[sbom_doc.name] = document
        return document

    def _new_spdx_document(self, sbom_doc: SBOMDocument) -> spdx.SpdxDocument:
        """Build the SpdxDocument shell: identity, namespaces, name and licensing."""
        document = spdx.SpdxDocument()

        # Use the document's own namespace when set, otherwise the global prefix.
        namespace = (
            sbom_doc.namespace.rstrip("/")
            if sbom_doc.namespace
            else self.sbom_data.namespace_prefix.rstrip("/")
        )
        document._id = self._shorten_id(f"{namespace}/documents/{sbom_doc.name}")

        for uri, prefix in self.namespace_prefixes.items():
            ns_map = spdx.NamespaceMap()
            ns_map.prefix = prefix
            ns_map.namespace = uri
            document.namespaceMap.append(ns_map)

        document.name = self._DOCUMENT_NAMES.get(
            sbom_doc.name, f"Zephyr {sbom_doc.name.capitalize()}"
        )
        document.creationInfo = self.creation_info

        data_license = self._create_license_expression("CC0-1.0")
        if data_license:
            document.dataLicense = data_license._id

        document.profileConformance.append(spdx.ProfileIdentifierType.core)
        document.profileConformance.append(spdx.ProfileIdentifierType.software)
        document.profileConformance.append(spdx.ProfileIdentifierType.simpleLicensing)
        # Only the build document conforms to the Build profile.
        if self.build and sbom_doc.name == self._BUILD_DOCUMENT:
            document.profileConformance.append(spdx.ProfileIdentifierType.build)
        return document

    def _collect_document_element_ids(self, sbom_doc: SBOMDocument, components) -> tuple[set, set]:
        """Gather the IDs of every element that belongs to this document.

        This spans the document's packages and files, the licenses they
        reference, the relationships connecting them, and the shared
        tool/agent/data-license elements.

        Returns a ``(element_ids, import_ids)`` pair: ``element_ids`` are defined
        by this document and serialized into it, while ``import_ids`` are elements
        referenced by this document's relationships but defined in another document
        (declared via ExternalMap rather than re-serialized here).
        """
        element_ids = self._package_and_file_ids(components)
        element_ids.update(self._referenced_license_ids(components))

        # Custom licenses recorded on the document must exist as elements even
        # if no package or file references them directly.
        self._register_custom_licenses(sbom_doc)

        # Seed the build document so its build-scoped relationships are collected.
        self._seed_build_element_ids(sbom_doc, element_ids)

        import_ids = self._collect_relationship_ids(sbom_doc, element_ids)

        if self.tool:
            element_ids.add(self.tool._id)
        if self.creator_agent:
            element_ids.add(self.creator_agent._id)
        data_license = self._create_license_expression("CC0-1.0")
        if data_license:
            element_ids.add(data_license._id)

        # A locally defined element is never also imported.
        import_ids -= element_ids

        return element_ids, import_ids

    def _package_and_file_ids(self, components) -> set:
        """IDs of the package and file elements contained in this document."""
        element_ids = set()
        for component in components:
            package = self.component_elements.get(component.name)
            if package:
                element_ids.add(package._id)
            for file_obj in component.files.values():
                file_element = self.file_elements.get(file_obj.path)
                if file_element:
                    element_ids.add(file_element._id)
        return element_ids

    def _referenced_license_ids(self, components) -> set:
        """License-expression IDs referenced by the document's packages and files."""
        license_ids = set()
        for component in components:
            license_ids.add(self._get_license_target_id(component.concluded_license))
            license_ids.add(self._get_license_target_id(component.declared_license))
            for file_obj in component.files.values():
                license_ids.add(self._get_license_target_id(file_obj.concluded_license))
                for lic in file_obj.license_info_in_file:
                    if lic != "NONE":
                        license_ids.add(self._get_license_target_id(lic))
        license_ids.discard(None)
        return license_ids

    def _register_custom_licenses(self, sbom_doc: SBOMDocument):
        """Ensure custom licenses stored on the document exist as elements."""
        for lic in sbom_doc.custom_license_ids:
            self._create_license_expression(lic)

    def _seed_build_element_ids(self, sbom_doc: SBOMDocument, element_ids: set):
        """Add the Build elements and build tool IDs to the build document's set.

        No-op for any other document, or when no Build element was produced.
        """
        if not self.build or sbom_doc.name != self._BUILD_DOCUMENT:
            return
        element_ids.add(self.build._id)
        for target_build in self.target_builds.values():
            element_ids.add(target_build._id)
        for tool in self.build_tools.values():
            element_ids.add(tool._id)

    def _collect_relationship_ids(self, sbom_doc: SBOMDocument, element_ids: set) -> set:
        """Add this document's relationships and their local endpoints to ``element_ids``.

        A relationship belongs to the document that owns its original
        (pre-reversal) "from" element. Ownership is tested against a snapshot
        of the document's own elements so that pulling in endpoints does not
        draw in unrelated relationships.

        Endpoints defined in another document are returned as the import set so
        the caller can declare them via ExternalMap instead of re-serializing
        them here.
        """
        owned = set(element_ids)
        home = self._element_home_index()
        this_doc = sbom_doc.name
        relationship_ids = set()
        endpoint_ids = set()
        import_ids = set()
        for rel in self.relationship_elements:
            from_id = getattr(rel, 'from_', None)
            original_from_id = self.relationship_original_from.get(rel._id, from_id)
            if original_from_id not in owned:
                continue
            relationship_ids.add(rel._id)
            endpoints = list(rel.to)
            if from_id:
                endpoints.append(from_id)
            for endpoint in endpoints:
                endpoint_home = home.get(endpoint)
                if endpoint_home is not None and endpoint_home != this_doc:
                    import_ids.add(endpoint)
                else:
                    endpoint_ids.add(endpoint)
        element_ids.update(relationship_ids)
        element_ids.update(endpoint_ids)
        return import_ids

    def _populate_document(
        self,
        document: spdx.SpdxDocument,
        element_ids: set,
        import_ids: set,
        components,
        sbom_doc: SBOMDocument,
    ):
        """Attach the selected elements, imports and root components to the document."""
        for element in self.elements:
            if self._belongs_in_document(element, document._id, element_ids):
                document.element.append(element)

        self._add_external_maps(document, import_ids)

        for component in components:
            package = self.component_elements.get(component.name)
            if package:
                document.rootElement.append(package)

        # The Build element is a root of the build document.
        if self.build and sbom_doc.name == self._BUILD_DOCUMENT:
            document.rootElement.append(self.build)

    def _add_external_maps(self, document: spdx.SpdxDocument, import_ids: set):
        """Declare elements used by, but defined outside, this document.

        Each foreign endpoint becomes an ExternalMap in the document's ``import`` set instead of
        being re-serialized here. ``locationHint`` points at the sibling JSON-LD file that defines
        the element so a consumer can retrieve it.
        """
        home_index = self._element_home_index()
        for import_id in sorted(import_ids):
            external_map = spdx.ExternalMap()
            external_map.externalSpdxId = import_id
            home = home_index.get(import_id)
            if home:
                external_map.locationHint = f"./{home}.jsonld"
            document.import_.append(external_map)

    @staticmethod
    def _belongs_in_document(element, document_id: str, element_ids: set) -> bool:
        """Whether ``element`` should be listed in the document's element set.

        Only proper SPDX ``Element`` objects (never the document itself) that
        were selected into ``element_ids`` qualify.
        """
        if not isinstance(element, spdx.Element) or isinstance(element, spdx.SpdxDocument):
            return False
        element_id = getattr(element, '_id', None)
        return bool(element_id) and element_id != document_id and element_id in element_ids

    # ---- SPDX 3.1 FunctionalSafety profile -------------------------------------

    def _create_functional_safety(self):
        """Emit the SPDX 3.1 FunctionalSafety graph from the traceability inputs.

        Builds ``Requirement`` elements and their refinement chain, ``Specification``
        elements for design artifacts, implementation ``software_Snippet``s (the
        ``z_impl_``/``z_vrfy_`` function bodies of each requirement's implementing
        symbols), and — from a twister run and its coverage matrix — test
        verifications whose evidence is the *actual* line coverage of the
        implementation. All of these are gathered into a standalone "safety"
        document by :meth:`_create_safety_document`.

        No-op unless SPDX 3.1 output was requested and a traceability graph was
        loaded into ``sbom_data.metadata['traceability']``.
        """
        if not self.is_31:
            return
        metadata = self.sbom_data.metadata
        traceability = metadata.get("traceability")
        if not traceability:
            return
        self._fs_catalog = metadata.get("requirements_catalog") or {}
        self._fs_results = metadata.get("test_results") or {}
        self._fs_impl_bodies = metadata.get("impl_bodies") or {}
        self._fs_coverage = metadata.get("coverage") or {}
        self._fs_all_covered = metadata.get("all_covered") or {}
        self._fs_environment = metadata.get("test_environment") or {}
        self._fs_source = metadata.get("source")
        self._fs_zephyr_base = metadata.get("zephyr_base") or ""
        self._fs_content_cache = {}
        self._fs_traceability = traceability
        # The test runner is the provenance for every verification/result.
        self._fs_twister_tool = self._fs_create_twister_tool()

        # 1) Requirements and the system -> software refinement chain.
        for uid in traceability.requirements:
            self._fs_get_requirement(uid)
        for req in traceability.requirements.values():
            requirement = self.requirement_elements.get(req.uid)
            if requirement is None:
                continue
            for parent_uid in req.traces_to:
                parent = self._fs_get_requirement(parent_uid)
                if parent is not None:
                    # The parent (system) requirement is refined to the detail of
                    # this (software) requirement.
                    self._fs_relationship(
                        spdx.RelationshipType.tracedToDetail, parent._id, [requirement._id]
                    )

        # 2) Design descriptions as Specifications that carry requirements.
        for design in traceability.designs.values():
            self._fs_specification(design)

        # 3) Implementation: each implementing symbol's resolved bodies become
        #    snippets linked from the requirement. Remember them per requirement so
        #    coverage can adjudicate which the verifying tests actually exercise.
        req_impl_snippets: dict[str, list] = {}
        for req in traceability.requirements.values():
            requirement = self.requirement_elements.get(req.uid)
            if requirement is None:
                continue
            snippets = []
            for symbol in req.implemented_by:
                for body in self._fs_impl_bodies.get(symbol, []):
                    snippet = self._fs_body_snippet(symbol, body)
                    if snippet is not None:
                        self._fs_relationship(
                            spdx.RelationshipType.implementedBy, requirement._id, [snippet._id]
                        )
                        snippets.append((body, snippet))
            if snippets:
                req_impl_snippets[req.uid] = snippets
            # "True traceability" adequacy: do the verifying tests actually exercise
            # the implementing code? Only meaningful for software requirements (a
            # system requirement is realized through the SRS that refine it), and
            # only when coverage was supplied.
            if self._fs_coverage and not req.is_system:
                self._fs_set_adequacy(req, requirement)

        # 4) Verification: each test measured by twister becomes a
        #    RequirementVerification + pass/fail EvaluationResult, with the
        #    implementation snippets its coverage actually exercised as evidence.
        for test in traceability.tests.values():
            record = self._fs_results.get(test.node_id)
            if record is None:
                continue
            requirements = [
                (uid, requirement)
                for uid in test.validates
                if (requirement := self.requirement_elements.get(uid)) is not None
            ]
            if requirements:
                self._fs_test_verification(test, record, requirements, req_impl_snippets)

    # ---- FunctionalSafety element builders -------------------------------------

    @property
    def _fs_namespace(self) -> str:
        return self.sbom_data.namespace_prefix.rstrip("/")

    def _fs_register(self, element):
        """Register an element for serialization in the safety document."""
        self.elements.append(element)
        self.fs_elements.append(element)
        return element

    def _fs_other_identifier(self, value: str, comment: str = ""):
        """Build an ``other``-type ExternalIdentifier carrying ``value``."""
        ext_id = spdx.ExternalIdentifier()
        ext_id.externalIdentifierType = spdx.ExternalIdentifierType.other
        ext_id.identifier = value
        if comment:
            ext_id.comment = comment
        return ext_id

    def _fs_relationship(self, rel_type, from_id: str, to_ids: list[str]):
        """Create a plain Relationship owned by the safety document.

        Its ``from`` endpoint is a FunctionalSafety element (requirement,
        specification, verification, ...), which no build document owns, so the
        standard per-document collection skips it and it is serialized only in the
        safety document.
        """
        rel = spdx.Relationship()
        rel._id = self._generate_relationship_id(len(self.relationship_elements))
        rel.relationshipType = rel_type
        rel.from_ = from_id
        rel.to = list(to_ids)
        rel.creationInfo = self.creation_info._id
        self.relationship_elements.append(rel)
        return self._fs_register(rel)

    def _fs_create_twister_tool(self):
        """Create the ``Tool`` element for the twister run that produced the results.

        Records the run provenance (Zephyr version/commit, run date, platform,
        toolchain and coverage tool) so every evaluation result can point at it
        with ``usesTool``. Returns ``None`` when no twister results were supplied.
        """
        if not self._fs_results:
            return None
        env = self._fs_environment
        options = env.get("options", {})
        platforms = options.get("platform") or sorted(
            {
                instance["platform"]
                for record in self._fs_results.values()
                for instance in record["instances"]
                if instance["platform"]
            }
        )

        tool = spdx.Tool()
        tool._id = self._shorten_id(f"{self._fs_namespace}/tools/twister")
        tool.creationInfo = self.creation_info._id
        tool.name = "Twister"
        tool.summary = "Zephyr twister test runner"
        for id_type, value in (
            ("zephyr-version", env.get("zephyr_version")),
            ("commit-date", env.get("commit_date")),
            ("run-date", env.get("run_date")),
            ("toolchain", env.get("toolchain")),
            ("host-os", env.get("os")),
            ("platform", ", ".join(platforms) if platforms else None),
            ("coverage-tool", options.get("coverage_tool")),
        ):
            if value:
                tool.externalIdentifier.append(self._fs_other_identifier(f"{id_type}:{value}"))
        return self._fs_register(tool)

    def _fs_set_adequacy(self, req, requirement):
        """Annotate a requirement with its "true traceability" adequacy verdict."""
        verdict = requirement_adequacy(
            req.implemented_by,
            req.validated_by,
            self._fs_impl_bodies,
            self._fs_coverage,
            self._fs_all_covered,
        )
        requirement.comment = f"True-traceability adequacy: {verdict} — {VERDICT_HELP[verdict]}"
        requirement.externalIdentifier.append(
            self._fs_other_identifier(f"adequacy:{verdict}", "True-traceability verdict")
        )

    def _fs_get_requirement(self, uid: str):
        """Return the ``Requirement`` for ``uid``, creating it on first use.

        Returns ``None`` when ``uid`` is not a node in the traceability graph (a
        dangling parent reference), so no empty requirement is emitted.
        """
        if uid in self.requirement_elements:
            return self.requirement_elements[uid]
        node = self._fs_traceability.requirements.get(uid)
        if node is None:
            return None

        info = self._fs_catalog.get(uid)
        title = node.title or (info.title if info else "")
        statement = (info.statement if info else "") or title or uid

        requirement = spdx.Requirement()
        requirement._id = self._shorten_id(f"{self._fs_namespace}/requirements/{uid}")
        requirement.creationInfo = self.creation_info._id
        requirement.name = f"{uid}: {title}" if title else uid
        if title:
            requirement.summary = title
        # requirementStatement is mandatory; fall back to the title, then the UID.
        requirement.requirementStatement = statement
        if info and info.rationale:
            requirement.requirementRationale = info.rationale
        requirement.externalIdentifier.append(
            self._fs_other_identifier(uid, "StrictDoc requirement UID")
        )
        if node.status:
            requirement.externalIdentifier.append(
                self._fs_other_identifier(f"status:{node.status}", "Requirement status")
            )
        if node.rtype:
            requirement.externalIdentifier.append(
                self._fs_other_identifier(f"requirement-type:{node.rtype}", "Requirement type")
            )

        self.requirement_elements[uid] = requirement
        return self._fs_register(requirement)

    def _fs_specification(self, design):
        """Create a ``Specification`` for a design node and link its requirements."""
        spec = spdx.Specification()
        slug = normalize_spdx_name(design.uid)
        spec._id = self._shorten_id(f"{self._fs_namespace}/specifications/{slug}")
        spec.creationInfo = self.creation_info._id
        spec.name = f"{design.uid}: {design.title}" if design.title else design.uid
        if design.title:
            spec.summary = design.title
        spec.specType = spdx.SpecificationType.specification
        spec.externalIdentifier.append(self._fs_other_identifier(design.uid, "Design element id"))
        if design.document:
            spec.externalIdentifier.append(
                self._fs_other_identifier(f"source-document:{design.document}", "Design document")
            )
        self.specification_elements[design.uid] = spec
        self._fs_register(spec)

        for req_uid in design.fulfills:
            requirement = self._fs_get_requirement(req_uid)
            if requirement is not None:
                self._fs_relationship(
                    spdx.RelationshipType.hasRequirement, spec._id, [requirement._id]
                )
        return spec

    def _fs_make_snippet(self, rel: str, start: int, end: int, slug_base: str, name: str):
        """Get or create a ``software_Snippet`` for ``rel:start-end``.

        Snippets are cached by ``(file, start, end)``. The name is stored on the
        snippet (and its span in the description) so the code is legible without
        resolving ``snippetFromFile``. Returns ``None`` when the host file is
        unavailable.
        """
        key = (rel, start, end)
        if key in self.snippet_elements:
            return self.snippet_elements[key]
        host = self._fs_host_file(rel)
        if host is None:
            return None

        snippet = spdx.software_Snippet()
        slug = normalize_spdx_name(f"{slug_base}-{start}-{end}")
        snippet._id = self._shorten_id(f"{self._fs_namespace}/snippets/{slug}")
        snippet.creationInfo = self.creation_info._id
        snippet.name = name
        snippet.description = f"{rel}:{start}-{end}"
        snippet.software_snippetFromFile = host._id

        line_range = spdx.PositiveIntegerRange()
        line_range.beginIntegerRange = start
        line_range.endIntegerRange = end
        snippet.software_lineRange = line_range

        content = self._fs_file_content(rel)
        if content is not None:
            byte_span = content_byte_range(content, start, end)
            if byte_span:
                byte_range = spdx.PositiveIntegerRange()
                byte_range.beginIntegerRange, byte_range.endIntegerRange = byte_span
                snippet.software_byteRange = byte_range

        self.snippet_elements[key] = snippet
        return self._fs_register(snippet)

    def _fs_body_snippet(self, symbol: str, body):
        """Snippet for a resolved implementation body (the whole function).

        ``body`` is a :class:`zspdx.sources.ImplBody`; the snippet is named for the
        actual function (``z_impl_``/``z_vrfy_`` prefixed).
        """
        fn_name = {"impl": f"z_impl_{symbol}", "vrfy": f"z_vrfy_{symbol}"}.get(body.variant, symbol)
        name = f"{fn_name} @ {body.file}:{body.start}-{body.end}"
        return self._fs_make_snippet(body.file, body.start, body.end, fn_name, name)

    @staticmethod
    def _contiguous_ranges(lines):
        """Collapse a sorted line list into inclusive contiguous ``(start, end)`` runs."""
        ranges: list[list[int]] = []
        for line in lines:
            if ranges and line == ranges[-1][1] + 1:
                ranges[-1][1] = line
            else:
                ranges.append([line, line])
        return [(start, end) for start, end in ranges]

    def _fs_coverage_snippets(self, rel: str, body, covered_lines):
        """Snippets for the code paths a test actually executed within ``body``.

        Returns one snippet per contiguous run of ``covered_lines`` that falls
        inside the body span, so the evidence reflects the executed paths rather
        than the whole function.
        """
        in_body = sorted(ln for ln in covered_lines if body.start <= ln <= body.end)
        snippets = []
        for start, end in self._contiguous_ranges(in_body):
            snippet = self._fs_make_snippet(
                rel, start, end, f"cov-{rel}", f"{rel}:{start}-{end}"
            )
            if snippet is not None:
                snippets.append(snippet)
        return snippets

    def _fs_file_content(self, rel: str) -> str | None:
        """Cached text of a tree-relative file, read at the coverage build's ref."""
        if rel not in self._fs_content_cache:
            if self._fs_source is not None:
                self._fs_content_cache[rel] = self._fs_source.read(rel)
            else:
                path = os.path.join(self._fs_zephyr_base, rel)
                try:
                    with open(path, encoding="utf-8", errors="replace") as f:
                        self._fs_content_cache[rel] = f.read()
                except OSError:
                    self._fs_content_cache[rel] = None
        return self._fs_content_cache[rel]

    def _fs_host_file(self, rel: str):
        """Return the ``software_File`` a snippet is carved from.

        Prefers a file already defined by the SBOM (referenced across documents via
        ExternalMap, never duplicated); otherwise synthesizes a file owned by the
        safety document (e.g. implementation sources absent from an application
        build). ``rel`` is a tree-relative path.
        """
        abs_path = os.path.normpath(os.path.join(self._fs_zephyr_base, rel))
        existing = self.file_elements.get(abs_path)
        if existing is not None:
            self.fs_import_ids.add(existing._id)
            return existing

        if rel in self.synth_file_elements:
            return self.synth_file_elements[rel]
        content = self._fs_file_content(rel)
        if content is None:
            return None

        file_element = spdx.software_File()
        file_element._id = self._generate_file_id(rel)
        file_element.name = self._fs_workspace_name(rel)
        file_element.creationInfo = self.creation_info._id
        file_element.software_fileKind = spdx.software_FileKindType.file
        file_element.software_primaryPurpose = spdx.software_SoftwarePurpose.source
        hash_obj = spdx.Hash()
        hash_obj.algorithm = spdx.HashAlgorithm.sha1
        hash_obj.hashValue = hashlib.sha1(content.encode()).hexdigest()
        file_element.verifiedUsing.append(hash_obj)

        self.synth_file_elements[rel] = file_element
        return self._fs_register(file_element)

    def _fs_workspace_name(self, rel: str) -> str:
        """A workspace-relative display name for a synthesized tree file."""
        if self._fs_zephyr_base:
            return f"{os.path.basename(self._fs_zephyr_base.rstrip('/'))}/{rel}"
        return rel

    def _fs_verdict_result_type(self, verdict: str):
        """Map a normalised verdict to a FunctionalSafety result type."""
        return {
            "pass": spdx.functionalsafety_EvaluationResultType.pass_,
            "fail": spdx.functionalsafety_EvaluationResultType.fail,
            "inconclusive": spdx.functionalsafety_EvaluationResultType.inconclusive,
        }[verdict]

    def _fs_test_verification(self, test, record, requirements, req_impl_snippets):
        """Emit the verification graph for one twister-measured test.

        Produces a ``functionalsafety_RequirementVerification`` (method ``test``)
        linked from every requirement it validates, a pass/fail
        ``functionalsafety_EvaluationResult`` from the run's rollup that records the
        twister tool via ``usesTool``, and — when the coverage matrix shows the test
        executed the implementation — a ``functionalsafety_EvidenceRelationship`` to
        the implementation snippets it actually exercised.
        """
        slug = normalize_spdx_name(test.node_id)
        verification = spdx.functionalsafety_RequirementVerification()
        verification._id = self._shorten_id(f"{self._fs_namespace}/verifications/{slug}")
        verification.creationInfo = self.creation_info._id
        verification.functionalsafety_verificationMethod.append(
            spdx.functionalsafety_VerificationType.test
        )
        verification.name = test.node_id
        if test.title:
            verification.summary = test.title
        verification.externalIdentifier.append(
            self._fs_other_identifier(test.node_id, "ztest case")
        )
        self._fs_register(verification)

        for _uid, requirement in requirements:
            self._fs_relationship(
                spdx.RelationshipType.verifiedBy, requirement._id, [verification._id]
            )

        verdict = verdict_from_rollup(record.get("rollup", ""))
        result = spdx.functionalsafety_EvaluationResult()
        result._id = self._shorten_id(f"{self._fs_namespace}/evaluations/{slug}")
        result.creationInfo = self.creation_info._id
        result.functionalsafety_evaluation = self._fs_verdict_result_type(verdict)
        result.functionalsafety_evaluationBasedOn = verification._id
        platforms = sorted({i["platform"] for i in record.get("instances", []) if i["platform"]})
        result.functionalsafety_evaluationRationale = (
            f"twister rollup '{record.get('rollup', 'no-run')}' over "
            f"{len(record.get('instances', []))} instance(s) on "
            f"{', '.join(platforms) or 'unknown platform'}"
        )
        result.name = f"Evaluation of {test.node_id}"
        self._fs_register(result)
        if self._fs_twister_tool is not None:
            self._fs_relationship(
                spdx.RelationshipType.usesTool, result._id, [self._fs_twister_tool._id]
            )

        # Coverage-backed evidence: snippets for the code paths this test actually
        # executed inside its requirements' implementation bodies -- the covered
        # line ranges, not the whole function.
        test_coverage = self._fs_coverage.get(test.node_id, {})
        covered = []
        seen = set()
        for uid, _requirement in requirements:
            for body, _impl_snippet in req_impl_snippets.get(uid, []):
                lines = test_coverage.get(body.file)
                if not lines:
                    continue
                for snippet in self._fs_coverage_snippets(body.file, body, lines):
                    if snippet._id not in seen:
                        covered.append(snippet)
                        seen.add(snippet._id)
        if covered:
            evidence = spdx.functionalsafety_EvidenceRelationship()
            evidence._id = self._generate_relationship_id(len(self.relationship_elements))
            evidence.relationshipType = spdx.RelationshipType.hasEvidence
            evidence.from_ = result._id
            evidence.to = [snippet._id for snippet in covered]
            evidence.functionalsafety_evidenceCategory.append(
                spdx.functionalsafety_EvidenceType.recording
            )
            evidence.creationInfo = self.creation_info._id
            self.relationship_elements.append(evidence)
            self._fs_register(evidence)

    def _create_safety_document(self):
        """Create the standalone document that owns the FunctionalSafety elements.

        The requirement/verification/snippet graph is not owned by any build
        component, so it cannot attach to the app, zephyr or build documents; it
        gets its own SPDX document instead. Source files referenced by snippets are
        declared via ExternalMap rather than duplicated. No-op unless FS elements
        were emitted.

        The FunctionalSafety profile identifier is not representable in the
        installed spdx_python_model bindings, so conformance is declared as
        ``core`` + ``software`` while still emitting the ``functionalsafety_*``
        elements the profile defines.
        """
        if not self.is_31 or not self.fs_elements:
            return

        document = spdx.SpdxDocument()
        document._id = self._shorten_id(f"{self._fs_namespace}/documents/safety")
        for uri, prefix in self.namespace_prefixes.items():
            ns_map = spdx.NamespaceMap()
            ns_map.prefix = prefix
            ns_map.namespace = uri
            document.namespaceMap.append(ns_map)
        document.name = "Zephyr Functional Safety"
        document.creationInfo = self.creation_info
        data_license = self._create_license_expression("CC0-1.0")
        if data_license:
            document.dataLicense = data_license._id
        document.profileConformance.append(spdx.ProfileIdentifierType.core)
        document.profileConformance.append(spdx.ProfileIdentifierType.software)

        # The data-license expression and creator agent are referenced by the
        # document/its creation info, so they must be serialized alongside it.
        extras = [element for element in (data_license, self.creator_agent) if element is not None]
        seen = set()
        for element in [*self.fs_elements, *extras]:
            element_id = getattr(element, "_id", None)
            if element_id and element_id not in seen:
                document.element.append(element)
                seen.add(element_id)

        # Reference source files carved by snippets via ExternalMap.
        self._add_external_maps(document, self.fs_import_ids)

        # The system-level requirements are the roots of the refinement tree.
        for uid, requirement in self.requirement_elements.items():
            node = self._fs_traceability.requirements.get(uid)
            if node is not None and node.is_system:
                document.rootElement.append(requirement)

        self.documents["safety"] = document

    def serialize(self, output_dir: str) -> bool:
        """Serialize SBOMData to SPDX 3.x format (JSON-LD)."""
        try:
            if not self._validate_inputs(output_dir):
                return False

            self._initialize_shared_objects()
            self._create_packages()
            self._create_files()
            self._create_relationships()
            self._create_contains_relationships()
            self._create_build_relationships()
            self._create_license_relationships()
            # SPDX 3.1 FunctionalSafety profile: requirements, coverage snippets and
            # test verifications imported from the traceability graph. No-op for 3.0.
            self._create_functional_safety()
            self._create_documents()
            # The FunctionalSafety graph is not owned by any build component, so it
            # lives in its own document created after the standard ones.
            self._create_safety_document()
            self._write_documents(output_dir)

            _logger.info(f"SPDX {self.spec_version} documents written to {output_dir}")
            return True

        except Exception:
            _logger.exception("Failed to serialize SPDX 3.0 document")
            return False

    def _validate_inputs(self, output_dir: str) -> bool:
        """Check that the serializer has everything it needs to run."""
        if not self.sbom_data:
            _logger.error("SBOMGraph is None or empty")
            return False
        if not self.sbom_data.namespace_prefix:
            _logger.error("Namespace prefix is required for SPDX 3.0")
            return False
        if not os.path.exists(output_dir):
            _logger.error(f"Output directory does not exist: {output_dir}")
            return False
        return True

    def _create_packages(self):
        """Create software_Package elements from all named components."""
        if not self.sbom_data.components:
            _logger.warning("No components found in SBOM data")
            return
        for component in self.sbom_data.components.values():
            if not component.name:
                _logger.warning("Skipping component with empty name")
                continue
            self._create_software_package(component)

    def _create_files(self):
        """Create software_File elements from all valid files."""
        if not self.sbom_data.files:
            _logger.warning("No files found in SBOM data")
            return
        for file_obj in self.sbom_data.files.values():
            if not file_obj.path:
                _logger.warning("Skipping file with empty path")
                continue
            self._create_software_file(file_obj)

    def _create_relationships(self):
        """Create the relationships declared by components and files.

        ``GENERATED_FROM`` edges are skipped: build provenance is carried by the Build profile
        (the Build's ``hasInput``/``hasOutput``), not by file-level ``generates`` edges like it
        would for SPDX 2.x.
        """
        for component in self.sbom_data.components.values():
            self._create_owner_relationships(component.relationships, "component", component.name)
        for file_obj in self.sbom_data.files.values():
            self._create_owner_relationships(file_obj.relationships, "file", file_obj.path)

    def _create_owner_relationships(self, relationships, owner_kind, owner_id):
        """Create each declared relationship, skipping ``GENERATED_FROM`` (see above)."""
        for rel in relationships:
            if rel.relationship_type == RelationshipType.GENERATED_FROM:
                continue
            if not self._create_relationship(rel):
                _logger.warning(f"Failed to create relationship from {owner_kind} {owner_id}")

    def _create_contains_relationships(self):
        """Create a CONTAINS relationship from each package to each of its files."""
        for component in self.sbom_data.components.values():
            package = self.component_elements.get(component.name)
            if not package:
                continue
            for file_obj in component.files.values():
                file_element = self.file_elements.get(file_obj.path)
                if file_element:
                    self._add_contains_relationship(package, file_element)

    def _add_contains_relationship(self, package, file_element):
        """Append a single package-contains-file relationship."""
        contains_rel = spdx.Relationship()
        contains_rel._id = self._generate_relationship_id(len(self.relationship_elements))
        contains_rel.relationshipType = spdx.RelationshipType.contains
        contains_rel.from_ = package._id
        contains_rel.to = [file_element._id]
        contains_rel.creationInfo = self.creation_info._id
        self.elements.append(contains_rel)
        self.relationship_elements.append(contains_rel)

    def _create_license_relationships(self):
        """Create concluded/declared license relationships for packages and files."""
        for component in self.sbom_data.components.values():
            package = self.component_elements.get(component.name)
            if not package:
                continue
            self._create_license_relationship(
                package._id,
                component.concluded_license,
                spdx.RelationshipType.hasConcludedLicense,
            )
            self._create_license_relationship(
                package._id,
                component.declared_license,
                spdx.RelationshipType.hasDeclaredLicense,
            )

        for file_obj in self.sbom_data.files.values():
            file_element = self.file_elements.get(file_obj.path)
            if not file_element:
                continue
            self._create_license_relationship(
                file_element._id,
                file_obj.concluded_license,
                spdx.RelationshipType.hasConcludedLicense,
            )
            for lic in file_obj.license_info_in_file or []:
                if lic != "NONE":
                    self._create_license_relationship(
                        file_element._id,
                        lic,
                        spdx.RelationshipType.hasDeclaredLicense,
                    )

    def _create_documents(self):
        """Create one SPDX document per non-empty SBOMDocument group."""
        for sbom_doc in self.sbom_data.documents.values():
            if sbom_doc.components:
                self._create_document(sbom_doc)

    def _write_documents(self, output_dir: str):
        """Write each created document to its own JSON-LD file."""
        for doc_name, document in self.documents.items():
            jsonld_path = os.path.join(output_dir, f"{doc_name}.jsonld")
            self._serialize_document_to_jsonld(document, jsonld_path)

    def _serialize_document_to_jsonld(self, document: spdx.SpdxDocument, output_path: str):
        """Serialize a single document to JSON-LD format."""
        object_set = spdx.SHACLObjectSet()
        for element in self._gather_elements_to_serialize(document):
            object_set.add(element)

        with open(output_path, "wb") as f:
            spdx.JSONLDSerializer().write(object_set, f, force_at_graph=True, indent=2)

        _logger.info(f"Written SPDX 3.0 JSON-LD to {output_path}")

    def _gather_elements_to_serialize(self, document: spdx.SpdxDocument) -> list:
        """Collect the document plus every element it references.

        The document, its creation info and the tool are always included; the
        remaining elements are looked up by the IDs listed in ``document.element``.
        """
        elements = [document]
        if self.creation_info:
            elements.append(self.creation_info)
        if self.tool:
            elements.append(self.tool)

        seen_ids = {getattr(elem, '_id', None) for elem in elements}

        referenced_ids = {
            elem._id for elem in getattr(document, 'element', []) if getattr(elem, '_id', None)
        }

        elements_by_id = {}
        for elem in self.elements:
            elem_id = getattr(elem, '_id', None)
            if elem_id and elem_id not in elements_by_id:
                elements_by_id[elem_id] = elem

        for elem_id in referenced_ids:
            if elem_id in seen_ids:
                continue
            elem = elements_by_id.get(elem_id)
            if elem is not None:
                elements.append(elem)
                seen_ids.add(elem_id)

        return elements
