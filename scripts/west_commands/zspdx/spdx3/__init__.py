# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import json
import os
from datetime import datetime, timezone

import yaml
from spdx_python_model import v3_0_1 as spdx
from west import log

from zspdx.cmakecache import parseCMakeCacheFile
from zspdx.cmakefileapijson import parseReply
from zspdx.getincludes import getCIncludes
from zspdx.spdxids import getUniqueFileID


class SPDX3NativeWalker:
    def __init__(self, config):
        self.config = config
        self.elements = []  # All SPDX3 elements
        self.software_packages = {}  # name -> software_Package
        self.files = {}  # path -> software_File
        self.relationships = []  # List of Relationship objects

        # Shared objects
        self.tool = None
        self.creation_info = None
        self.document = None

        # CMake data
        self.cmake_cache = {}
        self.codemodel = None
        self.compiler_path = ""
        self.sdk_path = ""
        self.meta_file = ""

        # Document organization
        self.build_packages = {}  # target_name -> software_Package
        self.app_package = None
        self.zephyr_package = None
        self.sdk_package = None
        self.modules_packages = {}  # module_name -> software_Package

        # Track file IDs for uniqueness
        self.times_seen = {}  # filename -> count

    def _initialize_shared_objects(self):
        """Initialize shared tool and creation info objects"""
        if self.tool is None:
            self.tool = spdx.Agent()
            self.tool._id = f"{self.config.namespacePrefix}/agents/west-spdx-tool"
            self.tool.name = "West SPDX Tool"
            self.tool.creationInfo = f"{self.config.namespacePrefix}/creationinfo"
            self.elements.append(self.tool)

        if self.creation_info is None:
            self.creation_info = spdx.CreationInfo()
            self.creation_info._id = f"{self.config.namespacePrefix}/creationinfo"
            self.creation_info.created = datetime.now(timezone.utc)
            self.creation_info.createdBy.append(self.tool._id)
            self.creation_info.specVersion = "3.0.1"
            self.elements.append(self.creation_info)

    def _create_software_package(
        self,
        name: str,
        purpose: spdx.software_SoftwarePurpose = spdx.software_SoftwarePurpose.library,
        url: str = None,
        version: str = None,
    ) -> spdx.software_Package:
        """Create a software package directly"""
        package_id = f"{self.config.namespacePrefix}/packages/{name}"

        package = spdx.software_Package()
        package._id = package_id
        package.name = name
        package.creationInfo = self.creation_info._id
        package.software_primaryPurpose = purpose
        package.software_downloadLocation = url or "NOASSERTION"
        package.software_copyrightText = "NOASSERTION"

        if version:
            package.software_packageVersion = version

        self.elements.append(package)
        self.software_packages[name] = package
        return package

    def _create_file(
        self, path: str, relative_path: str, package: spdx.software_Package, purpose: str = "source"
    ) -> spdx.software_File:
        print(f"Creating file {path} for package {package._id}")

        """Create a file directly and link it to its package"""
        # Use getUniqueFileID to generate a unique SPDX ID
        filename_only = os.path.basename(path)
        spdx_id = getUniqueFileID(filename_only, self.times_seen)
        file_id = f"{self.config.namespacePrefix}/files/{spdx_id}"

        file_obj = spdx.software_File()
        file_obj._id = file_id
        file_obj.name = relative_path
        file_obj.creationInfo = self.creation_info._id
        file_obj.software_fileKind = spdx.software_FileKindType.file

        # Map purpose to valid SoftwarePurpose values
        if purpose == "binary":
            # For binary files, use "source" as the closest valid purpose
            # since there's no specific "binary" purpose in SPDX 3.0
            file_obj.software_primaryPurpose = spdx.software_SoftwarePurpose.file
        else:
            file_obj.software_primaryPurpose = (
                f"https://spdx.org/rdf/3.0.1/terms/Software/SoftwarePurpose/{purpose}"
            )

        file_obj.software_copyrightText = "NOASSERTION"

        # Create CONTAINS relationship directly
        contains_rel = spdx.Relationship()
        contains_rel._id = f"{self.config.namespacePrefix}/relationships/{len(self.relationships)}"
        contains_rel.relationshipType = spdx.RelationshipType.contains
        contains_rel.from_ = package._id
        contains_rel.to = [file_obj._id]
        contains_rel.creationInfo = self.creation_info._id

        self.elements.append(file_obj)
        self.elements.append(contains_rel)
        self.relationships.append(contains_rel)
        self.files[path] = file_obj

        return file_obj

    def _create_relationship(
        self, from_element: str, to_element: str, relationship_type: spdx.RelationshipType
    ) -> spdx.Relationship:
        """Create a relationship directly between elements"""
        rel = spdx.Relationship()
        rel._id = f"{self.config.namespacePrefix}/relationships/{len(self.relationships)}"
        rel.relationshipType = relationship_type
        rel.from_ = from_element._id
        rel.to = [to_element._id]
        rel.creationInfo = self.creation_info._id

        self.elements.append(rel)
        self.relationships.append(rel)
        return rel

    def _create_document(self, name: str) -> spdx.SpdxDocument:
        """Create the main SPDX document"""
        self._initialize_shared_objects()

        doc_id = f"{self.config.namespacePrefix}/documents/{name}"

        document = spdx.SpdxDocument()
        document._id = doc_id
        document.name = name
        document.creationInfo = self.creation_info

        # Set data license
        license_expr = spdx.simplelicensing_LicenseExpression()
        license_expr._id = "https://spdx.org/licenses/CC0-1.0"
        license_expr.simplelicensing_licenseExpression = "CC0-1.0"
        license_expr.creationInfo = self.creation_info
        document.dataLicense = license_expr
        document.profileConformance.append(spdx.ProfileIdentifierType.core)

        # Add all elements to document
        for element in self.elements:
            if element._id:
                document.element.append(element._id)

        # Set root elements (main packages)
        for package in self.software_packages.values():
            document.rootElement.append(package)

        self.elements.append(document)
        self.document = document
        return document

    def parse_cmake_cache(self):
        """Parse CMake cache file"""
        cache_path = os.path.join(self.config.buildDir, "CMakeCache.txt")
        self.cmake_cache = parseCMakeCacheFile(cache_path)
        if self.cmake_cache:
            self.compiler_path = self.cmake_cache.get("CMAKE_C_COMPILER", "")
            self.sdk_path = self.cmake_cache.get("ZEPHYR_SDK_INSTALL_DIR", "")
            self.meta_file = self.cmake_cache.get("KERNEL_META_PATH", "")

    def parse_codemodel(self):
        """Parse CMake codemodel"""
        reply_dir = os.path.join(self.config.buildDir, ".cmake", "api", "v1", "reply")
        if not os.path.exists(reply_dir):
            log.err(f"CMake API reply directory {reply_dir} does not exist")
            return False

        # Find index file
        index_file = None
        for f in os.listdir(reply_dir):
            if f.startswith("index"):
                index_file = os.path.join(reply_dir, f)
                break

        if not index_file:
            log.err(f"CMake API reply index file not found in {reply_dir}")
            return False

        self.codemodel = parseReply(index_file)
        return self.codemodel is not None

    def setup_packages_from_meta(self):
        """Set up packages based on meta file information"""
        if not self.meta_file or not os.path.exists(self.meta_file):
            log.err("Meta file not found")
            return False

        try:
            with open(self.meta_file) as file:
                content = yaml.load(file.read(), yaml.SafeLoader)

            # Create app package
            self.app_package = self._create_software_package(
                "app-sources", spdx.software_SoftwarePurpose.application
            )

            # Create zephyr package
            zephyr_info = content.get("zephyr", {})
            self.zephyr_package = self._create_software_package(
                "zephyr-sources",
                spdx.software_SoftwarePurpose.library,
                zephyr_info.get("url"),
                zephyr_info.get("version"),
            )

            # Create SDK package if requested
            if self.config.includeSDK:
                self.sdk_package = self._create_software_package(
                    "sdk-sources", spdx.software_SoftwarePurpose.library
                )

            # Create module packages
            modules = content.get("modules", [])
            for module in modules:
                module_name = module.get("name", "unknown")
                module_security = module.get("security", {})
                external_refs = module_security.get("external-references", [])

                # Create package for module dependencies
                module_pkg = self._create_software_package(
                    f"{module_name}-deps", spdx.software_SoftwarePurpose.library
                )

                # Add external references as external identifiers
                # TODO validate with the regexps from writer.py
                for ref in external_refs:
                    if ref.startswith("cpe:"):
                        cpe_id = spdx.ExternalIdentifier()
                        cpe_id.externalIdentifierType = spdx.ExternalIdentifierType.cpe23
                        cpe_id.identifier = ref
                        module_pkg.externalIdentifier.append(cpe_id)
                    elif ref.startswith("pkg:"):
                        if not hasattr(module_pkg, 'software_packageUrl'):
                            module_pkg.software_packageUrl = ref
                        else:
                            purl_id = spdx.ExternalIdentifier()
                            purl_id.externalIdentifierType = spdx.ExternalIdentifierType.packageUrl
                            purl_id.identifier = ref
                            module_pkg.externalIdentifier.append(purl_id)

                self.modules_packages[module_name] = module_pkg

            return True

        except Exception as e:
            log.err(f"Failed to parse meta file: {e}")
            return False

    def walk_targets(self):
        """Walk through CMake targets and create SPDX3 objects directly"""
        if not self.codemodel or not self.codemodel.configurations:
            return

        for config in self.codemodel.configurations:
            for config_target in config.configTargets:
                if config_target.target:
                    self._process_target(config_target.target)

    def _process_target(self, target):
        """Process a single CMake target"""
        target_name = target.name
        target_type = target.type.name if target.type else 'UNKNOWN'

        # Create package for this target
        purpose = (
            spdx.software_SoftwarePurpose.application
            if target_type == 'EXECUTABLE'
            else spdx.software_SoftwarePurpose.library
        )
        package = self._create_software_package(target_name, purpose)
        self.build_packages[target_name] = package

        # Process build artifacts
        build_files = []
        for artifact in target.artifacts:
            build_file = self._process_build_artifact(artifact, package)
            if build_file:
                build_files.append(build_file)

        # Process source files and create relationships
        for source_group in target.sourceGroups:
            for source in source_group.sources:
                source_file = self._process_source_file(source, package, target)
                if source_file and build_files:
                    # Create GENERATED_FROM relationship for each build file
                    for build_file in build_files:
                        self._create_relationship(
                            source_file, build_file, spdx.RelationshipType.generates
                        )

        # Process target dependencies
        self._process_target_dependencies(target, package, build_files)

    def _process_build_artifact(self, artifact_path, package):
        """Process a build artifact"""
        if not artifact_path:
            return None

        # Create file object for build artifact
        file_obj = self._create_file(
            artifact_path, os.path.basename(artifact_path), package, "binary"
        )
        return file_obj

    def _process_source_file(self, source, package, target):
        """Process a source file and create SPDX3 file object directly"""
        source_path = source.path

        # Resolve relative source paths using target or codemodel source roots
        if source_path and not os.path.isabs(source_path):
            candidate_paths = []
            if getattr(target, "paths_source", ""):
                candidate_paths.append(os.path.join(target.paths_source, source_path))
            if self.codemodel and getattr(self.codemodel, "paths_source", ""):
                candidate_paths.append(os.path.join(self.codemodel.paths_source, source_path))

            for candidate in candidate_paths:
                abs_candidate = os.path.normpath(candidate)
                if os.path.exists(abs_candidate):
                    source_path = abs_candidate
                    break

        if not source_path or not os.path.exists(source_path):
            return None

        # Determine which package should own this file
        owning_package = self._find_owning_package(source_path)
        relative_path = os.path.relpath(source_path, self.config.buildDir)

        file_obj = self._create_file(source_path, relative_path, owning_package, "source")

        # Process includes if configured
        if self.config.analyzeIncludes:
            self._process_includes(source_path, file_obj)

        return file_obj

    def _process_includes(self, source_path, file_obj):
        """Process included headers for a source file"""
        if not self.compiler_path:
            return

        # Find the compile group for this source file
        compile_group = None
        for config in self.codemodel.configurations:
            for config_target in config.configTargets:
                if config_target.target:
                    for source_group in config_target.target.sourceGroups:
                        for source in source_group.sources:
                            # Match either directly (exact) or via absolutized path under target source dir
                            if source.path == source_path or (
                                getattr(config_target.target, "paths_source", "")
                                and os.path.normpath(
                                    os.path.join(config_target.target.paths_source, source.path)
                                )
                                == source_path
                            ):
                                # Get the compile group for this source
                                if (
                                    len(config_target.target.compileGroups)
                                    > source.compileGroupIndex
                                ):
                                    compile_group = config_target.target.compileGroups[
                                        source.compileGroupIndex
                                    ]
                                    break
                        if compile_group:
                            break
                    if compile_group:
                        break
                if compile_group:
                    break
            if compile_group:
                break

        if not compile_group:
            return

        # Only process C files
        if compile_group.language != "C":
            return

        includes = getCIncludes(self.compiler_path, source_path, compile_group)
        for include_path in includes:
            if os.path.exists(include_path):
                # Create file object for include if it doesn't exist
                if include_path not in self.files:
                    include_rel_path = os.path.relpath(include_path, self.config.buildDir)
                    include_file = self._create_file(
                        include_path, include_rel_path, self._find_owning_package(include_path)
                    )

                # Create INCLUDES relationship (using OTHER type)
                self._create_relationship(
                    file_obj,
                    self.files[include_path],
                    spdx.RelationshipType.other,
                )

    def _process_target_dependencies(self, target, package, build_files):
        """Process target dependencies and create relationships"""
        if not self.codemodel or not self.codemodel.configurations:
            return

        config = self.codemodel.configurations[0]

        for dep in target.dependencies:
            # Extract dependency name from ID
            dep_fragments = dep.id.split(":")
            dep_name = dep_fragments[0]

            # Find the dependency package
            dep_package = self.build_packages.get(dep_name)
            if dep_package:
                # Create HAS_PREREQUISITE relationship
                self._create_relationship(
                    package, dep_package, spdx.RelationshipType.hasPrerequisite
                )

                # Create STATIC_LINK relationship between build files if both have artifacts
                if build_files:
                    # Find dependency's build files
                    for config_target in config.configTargets:
                        if config_target.name == dep_name and config_target.target.artifacts:
                            for artifact in config_target.target.artifacts:
                                dep_file_path = artifact
                                if dep_file_path in self.files:
                                    dep_build_file = self.files[dep_file_path]
                                    # Create STATIC_LINK relationship for each build file
                                    for build_file in build_files:
                                        self._create_relationship(
                                            build_file,
                                            dep_build_file,
                                            spdx.RelationshipType.hasStaticLink,
                                        )

    def _find_owning_package(self, file_path):
        """Find which package should own a file based on path"""
        # Simple heuristic - could be enhanced
        if 'zephyr' in file_path.lower():
            return self.zephyr_package or list(self.software_packages.values())[0]
        elif 'app' in file_path.lower():
            return self.app_package or list(self.software_packages.values())[0]
        elif self.sdk_path and self.sdk_path in file_path:
            return self.sdk_package or list(self.software_packages.values())[0]
        else:
            return list(self.software_packages.values())[0]

    def generate_sbom(self) -> bool:
        """Main entry point to generate the SBOM"""
        try:
            log.inf("Parsing CMake cache file")
            self.parse_cmake_cache()

            if not self.meta_file:
                log.err("CONFIG_BUILD_OUTPUT_META must be enabled to generate SPDX files")
                return False

            log.inf("Parsing CMake codemodel")
            if not self.parse_codemodel():
                return False

            # Initialize shared objects before creating packages
            self._initialize_shared_objects()

            log.inf("Setting up packages from meta file")
            if not self.setup_packages_from_meta():
                return False

            log.inf("Walking through targets")
            self.walk_targets()

            # log.inf("Creating SPDX document")
            # document = self._create_document("Zephyr Native SPDX3 SBOM")

            # # Create DESCRIBES relationships for main packages
            # if self.app_package:
            #     self._create_relationship(
            #         document, self.app_package, spdx.RelationshipType.describes
            #     )
            # if self.zephyr_package:
            #     self._create_relationship(
            #         document, self.zephyr_package, spdx.RelationshipType.describes
            #     )
            # if self.sdk_package:
            #     self._create_relationship(
            #         document, self.sdk_package, spdx.RelationshipType.describes
            #     )

            log.inf("Serializing to JSON")
            self._serialize_to_json()

            return True

        except Exception as e:
            log.err(f"Failed to generate SPDX 3.0 document: {e}")
            return False

    def _serialize_to_json(self):
        """Serialize to JSON-LD format"""
        output_file = os.path.join(self.config.spdxDir, "zephyr-spdx3-native")

        # Create JSON-LD structure
        elements_data = []
        for element in self.elements:
            encoder = spdx.JSONLDEncoder()
            state = spdx.EncodeState()
            element.encode(encoder, state)
            if encoder.data:
                elements_data.append(encoder.data)

        complete_dict = {
            "@context": "https://spdx.org/rdf/3.0.1/spdx-context.jsonld",
            "@graph": elements_data,
        }

        # Write JSON-LD
        with open(output_file + ".jsonld", "w") as f:
            json.dump(complete_dict, f, indent=2)

        log.inf(f"Written SPDX 3.0 JSON-LD to {output_file}.jsonld")
