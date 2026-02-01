# Copyright (c) 2020-2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import os
import re
import subprocess
from dataclasses import dataclass

import yaml
from west import log
from west.util import WestNotFound, west_topdir

from zspdx.cmakefileapijson import parseReply
from zspdx.getincludes import getCIncludes
from zspdx.model import (
    ComponentPurpose,
    SBOMBuild,
    SBOMComponent,
    SBOMData,
    SBOMFile,
    SBOMRelationship,
    SBomDocument,
)
from datetime import datetime, timezone


def get_tool_version(tool_path: str) -> str:
    """Get the version string from a build tool by running it with --version.

    Args:
        tool_path: Path to the tool executable

    Returns:
        Version string if successfully extracted, empty string otherwise
    """
    if not tool_path or not os.path.isfile(tool_path):
        return ""

    try:
        # Run the tool with --version flag
        result = subprocess.run(
            [tool_path, "--version"],
            capture_output=True,
            text=True,
            timeout=5,  # 5 second timeout to avoid hanging
        )
        output = result.stdout or result.stderr

        if not output:
            return ""

        # Parse the version from the first line of output
        # Common formats:
        # - "gcc (Zephyr SDK 0.17.4) 12.2.0" -> "12.2.0"
        # - "GNU ld (Zephyr SDK 0.17.4) 2.38" -> "2.38"
        # - "GNU ar (Zephyr SDK 0.17.4) 2.38" -> "2.38"
        # - "clang version 15.0.0" -> "15.0.0"
        # - "cmake version 3.28.1" -> "3.28.1"
        first_line = output.strip().split('\n')[0]

        # Try to extract version number using common patterns
        # Pattern 1: "version X.Y.Z" or "version X.Y"
        match = re.search(r'version\s+(\d+\.\d+(?:\.\d+)?)', first_line, re.IGNORECASE)
        if match:
            return match.group(1)

        # Pattern 2: Last token that looks like a version (e.g., "gcc ... 12.2.0")
        match = re.search(r'\b(\d+\.\d+(?:\.\d+)?)\s*$', first_line)
        if match:
            return match.group(1)

        # Pattern 3: Any version-like pattern in the line
        match = re.search(r'\b(\d+\.\d+(?:\.\d+)?)\b', first_line)
        if match:
            return match.group(1)

        return ""
    except (subprocess.TimeoutExpired, subprocess.SubprocessError, OSError) as e:
        log.dbg(f"Could not get version for {tool_path}: {e}")
        return ""


# WalkerConfig contains configuration data for the Walker.
@dataclass(eq=True)
class WalkerConfig:
    # prefix for Document namespaces; should not end with "/"
    namespacePrefix: str = ""

    # location of build directory
    buildDir: str = ""

    # should also analyze for included header files?
    analyzeIncludes: bool = False

    # should also add an SPDX document for the SDK?
    includeSDK: bool = False


# Walker is the main analysis class: it walks through the CMake codemodel,
# build files, and corresponding source and SDK files, and gathers the
# information needed to build the SBOM data.
class Walker:
    # initialize with WalkerConfig
    def __init__(self, cfg):
        super().__init__()

        # configuration - WalkerConfig
        self.cfg = cfg

        # SBOM data container
        self.sbom_data = SBOMData()
        self.sbom_data.namespace_prefix = cfg.namespacePrefix
        self.sbom_data.build_dir = cfg.buildDir

        # Component references for easy access
        self.component_app = None
        self.component_zephyr = None
        self.component_sdk = None
        self.component_build_targets = {}  # target_name -> SBOMComponent
        self.component_modules_deps = {}  # module_name -> SBOMComponent

        # Document references for easy access
        self.doc_app = None
        self.doc_zephyr = None
        self.doc_sdk = None
        self.doc_build = None
        self.doc_modules_deps = None

        # queue of pending source file paths to create, process and assign
        self.pendingSources = []

        # queue of pending relationship data to create, process and assign
        # Format: (from_type, from_identifier, to_type, to_identifier, relationship_type)
        # Types: "component", "file"
        self.pendingRelationships = []

        # parsed CMake file API reply (contains codemodel, cache, toolchains)
        self.cmakeApiReply = None

        # parsed CMake codemodel (shortcut reference)
        self.cm = None

        # CMake cache (shortcut reference)
        self.cmakeCache = None

        # CMake toolchains (shortcut reference)
        self.toolchains = None

        # C compiler path from CMake file API
        self.compilerPath = ""

        # SDK install path from CMake cache
        self.sdkPath = ""

        # Meta file path
        self.metaFile = ""

    def _build_purl(self, url, version=None):
        if not url:
            return None

        purl = None
        # This is designed to match repository with the following url pattern:
        # '<protocol><type>/<namespace>/<package>
        COMMON_GIT_URL_REGEX=r'((git@|http(s)?:\/\/)(?P<type>[\w\.@]+)(\.\w+)(\/|:))(?P<namespace>[\w,\-,\_\/]+)\/(?P<package>[\w,\-,\_]+)(.git){0,1}((\/){0,1})$'

        match = re.fullmatch(COMMON_GIT_URL_REGEX, url)
        if match:
            purl = f'pkg:{match.group("type")}/{match.group("namespace")}/{match.group("package")}'

        if purl and version:
            purl += f'@{version}'

        return purl

    # primary entry point
    def collectSBOMData(self):
        """
        Collect SBOM data from CMake codemodel and build artifacts.
        Returns SBOMData object containing all collected information.
        """
        # parse CMake file API reply (codemodel, cache, toolchains)
        log.inf("parsing CMake file API reply")
        self.cmakeApiReply = self.getFileApiReply()
        if not self.cmakeApiReply:
            log.err("could not parse CMake file API reply; bailing")
            return None

        # set up shortcut references
        self.cm = self.cmakeApiReply.codemodel
        self.cmakeCache = self.cmakeApiReply.cache
        self.toolchains = self.cmakeApiReply.toolchains

        # extract build info from file API data
        log.inf("extracting build information from CMake file API")
        self.extractBuildInfo()

        # check if meta file is generated
        if not self.metaFile:
            log.err("CONFIG_BUILD_OUTPUT_META must be enabled to generate spdx files; bailing")
            return None

        # set up components
        log.inf("setting up SBOM components")
        retval = self.setupComponents()
        if not retval:
            return None

        # walk through targets in codemodel to gather information
        log.inf("walking through targets")
        self.walkTargets()

        # walk through pending sources and create corresponding files
        log.inf("walking through pending sources files")
        self.walkPendingSources()

        # walk through pending relationship data and create relationships
        log.inf("walking through pending relationships")
        self.walkRelationships()

        return self.sbom_data

    # extract build info from CMake file API data
    def extractBuildInfo(self):
        """Extract build information from CMake file API (cache and toolchains)."""
        # Get compiler path from toolchains (preferred) or cache (fallback)
        if self.toolchains:
            self.compilerPath = self.toolchains.get_compiler_path("C")
            log.dbg(f"C compiler path from toolchains: {self.compilerPath}")

        # Get paths from cache as fallback or for values not in toolchains
        if self.cmakeCache:
            if not self.compilerPath:
                self.compilerPath = self.cmakeCache.get("CMAKE_C_COMPILER", "")
                log.dbg(f"C compiler path from cache: {self.compilerPath}")

            self.sdkPath = self.cmakeCache.get("ZEPHYR_SDK_INSTALL_DIR", "")
            self.metaFile = self.cmakeCache.get("KERNEL_META_PATH", "")

            # Store build information in SBOM metadata for SPDX 3.0 Build Profile
            build_info = {}

            # Get compiler paths from toolchains (more accurate) or cache
            if self.toolchains:
                build_info["cmake_compiler"] = self.toolchains.get_compiler_path("C")
                build_info["cmake_cxx_compiler"] = self.toolchains.get_compiler_path("CXX")
                build_info["cmake_asm_compiler"] = self.toolchains.get_compiler_path("ASM")
                # Get compiler versions directly from toolchains (no need to run --version)
                build_info["c_compiler_version"] = self.toolchains.get_compiler_version("C")
                build_info["c_compiler_id"] = self.toolchains.get_compiler_id("C")
                build_info["cxx_compiler_version"] = self.toolchains.get_compiler_version("CXX")
                build_info["cxx_compiler_id"] = self.toolchains.get_compiler_id("CXX")
                build_info["asm_compiler_version"] = self.toolchains.get_compiler_version("ASM")
                build_info["asm_compiler_id"] = self.toolchains.get_compiler_id("ASM")
            else:
                # Fallback to cache
                build_info["cmake_compiler"] = self.cmakeCache.get("CMAKE_C_COMPILER", "")
                build_info["cmake_cxx_compiler"] = self.cmakeCache.get("CMAKE_CXX_COMPILER", "")
                build_info["cmake_asm_compiler"] = self.cmakeCache.get("CMAKE_ASM_COMPILER", "")

            # These are always from cache
            build_info["cmake_linker"] = self.cmakeCache.get("CMAKE_LINKER", "")
            build_info["cmake_ar"] = self.cmakeCache.get("CMAKE_AR", "")
            build_info["cmake_build_type"] = self.cmakeCache.get("CMAKE_BUILD_TYPE", "")
            build_info["cmake_system_name"] = self.cmakeCache.get("CMAKE_SYSTEM_NAME", "")
            build_info["cmake_system_processor"] = self.cmakeCache.get("CMAKE_SYSTEM_PROCESSOR", "")

            # Get CMake info from the file API index
            if self.cmakeApiReply and self.cmakeApiReply.cmake_info:
                cmake_info = self.cmakeApiReply.cmake_info
                build_info["cmake_generator"] = cmake_info.generator_name
                build_info["cmake_version"] = cmake_info.version_string
                build_info["cmake_path"] = cmake_info.cmake_path

            # Capture tool versions for linker and ar (not in toolchains)
            log.dbg("capturing build tool versions for linker and ar")
            tool_paths = {
                "linker_version": build_info.get("cmake_linker", ""),
                "ar_version": build_info.get("cmake_ar", ""),
            }
            for version_key, tool_path in tool_paths.items():
                if tool_path:
                    version = get_tool_version(tool_path)
                    if version:
                        build_info[version_key] = version

            self.sbom_data.metadata["build_info"] = build_info

            # Populate SBOMBuild object
            build = SBOMBuild(
                id=f"build-{self.cfg.namespacePrefix.split('/')[-1]}",
                build_type=build_info.get("cmake_build_type", ""),
            )

            # Note on build timestamps:
            # We cannot accurately determine build start/end times from the build directory.
            # Per SPDX 3.0 spec, these fields are optional and may be omitted for reproducibility.
            # We leave them unpopulated rather than providing inaccurate timestamps.
            # If accurate timestamps are needed, the build system should record them explicitly.

            self.sbom_data.build = build

    # determine path from build dir to CMake file-based API index file, then
    # parse it and return the CMakeFileApiReply
    def getFileApiReply(self):
        log.dbg("getting CMake file API reply files")

        # make sure the reply directory exists
        cmakeReplyDirPath = os.path.join(self.cfg.buildDir, ".cmake", "api", "v1", "reply")
        if not os.path.exists(cmakeReplyDirPath):
            log.err(f'cmake api reply directory {cmakeReplyDirPath} does not exist')
            log.err('was query directory created before cmake build ran?')
            return None
        if not os.path.isdir(cmakeReplyDirPath):
            log.err(f'cmake api reply directory {cmakeReplyDirPath} exists but is not a directory')
            return None

        # find file with "index" prefix; there should only be one
        indexFilePath = ""
        for f in os.listdir(cmakeReplyDirPath):
            if f.startswith("index"):
                indexFilePath = os.path.join(cmakeReplyDirPath, f)
                break
        if indexFilePath == "":
            # didn't find it
            log.err(f'cmake api reply index file not found in {cmakeReplyDirPath}')
            return None

        # parse it
        return parseReply(indexFilePath)

    def _create_document(self, name: str) -> SBomDocument:
        """Create a document with the given name and register it with SBOM data."""
        doc = SBomDocument()
        doc.name = name
        doc.namespace = f"{self.cfg.namespacePrefix}/{name}"
        self.sbom_data.add_document(doc)
        return doc

    def setupDocuments(self):
        """Set up all SBOM documents."""
        log.dbg("setting up SBOM documents")

        # Create core documents
        self.doc_app = self._create_document("app")
        self.doc_zephyr = self._create_document("zephyr")
        self.doc_build = self._create_document("build")
        self.doc_modules_deps = self._create_document("modules-deps")

        # SDK document is optional
        if self.cfg.includeSDK:
            self.doc_sdk = self._create_document("sdk")

    def setupComponents(self):
        """Set up all SBOM components from meta file and configuration."""
        log.dbg("setting up SBOM components")

        # First set up documents
        self.setupDocuments()

        try:
            with open(self.metaFile) as file:
                content = yaml.load(file.read(), yaml.SafeLoader)
                if not self.setupZephyrComponent(content["zephyr"], content["modules"]):
                    return False
        except (FileNotFoundError, yaml.YAMLError):
            log.err("cannot find a valid zephyr.meta required for SPDX generation; bailing")
            return False

        self.setupAppComponent()

        if self.cfg.includeSDK:
            self.setupSDKComponent()

        self.setupModulesDepsComponent(content["modules"])

        return True

    def setupAppComponent(self):
        """Set up app sources component."""
        component = SBOMComponent()
        component.name = "app-sources"
        component.purpose = ComponentPurpose.SOURCE
        component.base_dir = self.cm.paths_source
        component.declared_license = "NOASSERTION"
        component.copyright_text = "NOASSERTION"

        self.sbom_data.add_component(component, "app")
        self.doc_app.add_component(component)
        self.component_app = component

    def setupZephyrComponent(self, zephyr, modules):
        """Set up zephyr sources component and module components."""
        # relativeBaseDir is Zephyr sources topdir
        try:
            relativeBaseDir = west_topdir(self.cm.paths_source)
        except WestNotFound:
            log.err("cannot find west_topdir for CMake Codemodel sources path "
                    f"{self.cm.paths_source}; bailing")
            return False

        # set up zephyr sources component
        component = SBOMComponent()
        component.name = "zephyr-sources"
        component.purpose = ComponentPurpose.SOURCE
        component.base_dir = relativeBaseDir
        component.declared_license = "NOASSERTION"
        component.copyright_text = "NOASSERTION"

        zephyr_url = zephyr.get("remote", "")
        if zephyr_url:
            component.url = zephyr_url

        if zephyr.get("revision"):
            component.revision = zephyr.get("revision")

        purl = None
        zephyr_tags = zephyr.get("tags", "")
        if zephyr_tags:
            # Find tag vX.Y.Z
            for tag in zephyr_tags:
                version = re.fullmatch(r'^v(?P<version>\d+\.\d+\.\d+)$', tag)
                purl = self._build_purl(zephyr_url, tag)

                if purl:
                    component.external_references.append(purl)

                # Extract version from tag once
                if component.version == "" and version:
                    component.version = version.group('version')

        if len(component.version) > 0:
            cpe = f'cpe:2.3:o:zephyrproject:zephyr:{component.version}:-:*:*:*:*:*:*'
            component.external_references.append(cpe)

        self.sbom_data.add_component(component, "zephyr")
        self.doc_zephyr.add_component(component)
        self.component_zephyr = component

        return True

    def setupSDKComponent(self):
        """Set up SDK sources component."""
        component = SBOMComponent()
        component.name = "sdk-sources"
        component.purpose = ComponentPurpose.SOURCE
        component.base_dir = self.sdkPath
        component.declared_license = "NOASSERTION"
        component.copyright_text = "NOASSERTION"

        self.sbom_data.add_component(component, "sdk")
        self.doc_sdk.add_component(component)
        self.component_sdk = component

    def setupModulesDepsComponent(self, modules):
        """Set up module dependency components."""
        for module in modules:
            module_name = module.get("name", None)
            module_security = module.get("security", None)

            if not module_name:
                log.err("cannot find module name in meta file; bailing")
                return False

            module_ext_ref = []
            if module_security:
                module_ext_ref = module_security.get("external-references", [])

            # set up module deps component
            component = SBOMComponent()
            component.name = module_name + "-deps"
            component.purpose = ComponentPurpose.LIBRARY
            component.declared_license = "NOASSERTION"
            component.copyright_text = "NOASSERTION"

            for ref in module_ext_ref:
                component.external_references.append(ref)

            self.sbom_data.add_component(component, "modules-deps")
            self.doc_modules_deps.add_component(component)
            self.component_modules_deps[module_name] = component

        return True

    # walk through targets and gather information
    def walkTargets(self):
        log.dbg("walking targets from codemodel")

        # assuming just one configuration; consider whether this is incorrect
        cfgTargets = self.cm.configurations[0].configTargets
        for cfgTarget in cfgTargets:
            # build the Component for this target
            component = self.initConfigTargetComponent(cfgTarget)

            # see whether this target has any build artifacts at all
            if len(cfgTarget.target.artifacts) > 0:
                # add its build file
                bf = self.addBuildFile(cfgTarget, component)
                if component.name == "zephyr_final":
                    component.purpose = ComponentPurpose.APPLICATION
                else:
                    component.purpose = ComponentPurpose.LIBRARY

                # capture build metadata from CMake model
                self.captureBuildMetadata(cfgTarget, component, bf)

                # get its source files if build file is found
                if bf:
                    self.collectPendingSourceFiles(cfgTarget, component, bf)
            else:
                log.dbg(f"  - target {cfgTarget.name} has no build artifacts")

            # get its target dependencies
            self.collectTargetDependencies(cfgTargets, cfgTarget, component)

    # capture build metadata from CMake model for a target
    def captureBuildMetadata(self, cfgTarget, component, bf):
        """Extract build metadata from CMake target to enable accurate tool relationships.

        This captures which compilers/tools were actually used based on CMake's
        compileGroups, link_language, and archive info rather than file extension heuristics.
        """
        target = cfgTarget.target

        # Store target type (EXECUTABLE, STATIC_LIBRARY, etc.)
        component.metadata["target_type"] = target.type.name

        # Collect languages from compile groups
        compile_languages = set()
        for cg in target.compileGroups:
            if cg.language:
                compile_languages.add(cg.language)
        component.metadata["compile_languages"] = list(compile_languages)

        # Link language (for executables and shared libraries)
        if target.link_language:
            component.metadata["link_language"] = target.link_language

        # Archive info (for static libraries)
        if target.archive_commandFragments:
            component.metadata["is_archive"] = True

        # Also store in the build file metadata for reference
        if bf:
            bf.metadata["target_type"] = target.type.name
            bf.metadata["compile_languages"] = list(compile_languages)
            if target.link_language:
                bf.metadata["link_language"] = target.link_language
            if target.archive_commandFragments:
                bf.metadata["is_archive"] = True

        log.dbg(f"    - build metadata: type={target.type.name}, "
                f"languages={compile_languages}, link={target.link_language}")

    # build a Component for the given ConfigTarget
    def initConfigTargetComponent(self, cfgTarget):
        log.dbg(f"  - initializing Component for target: {cfgTarget.name}")

        # create target Component
        component = SBOMComponent()
        component.name = cfgTarget.name
        component.base_dir = self.cm.paths_build
        component.declared_license = "NOASSERTION"
        component.copyright_text = "NOASSERTION"

        # add Component to SBOM data and build document
        self.sbom_data.add_component(component, "build")
        self.doc_build.add_component(component)
        self.component_build_targets[cfgTarget.name] = component
        return component

    # create a target's build product File and add it to its Component
    # call with:
    #   1) ConfigTarget
    #   2) Component for that target
    # returns: SBOMFile
    def addBuildFile(self, cfgTarget, component):
        # assumes only one artifact in each target
        artifactPath = os.path.join(component.base_dir, cfgTarget.target.artifacts[0])
        log.dbg(f"  - adding File {artifactPath}")
        log.dbg(f"    - base_dir: {component.base_dir}")
        log.dbg(f"    - artifacts[0]: {cfgTarget.target.artifacts[0]}")

        # don't create build File if artifact path points to nonexistent file
        if not os.path.exists(artifactPath):
            log.dbg(f"  - target {cfgTarget.name} lists build artifact {artifactPath} "
                    "but file not found after build; skipping")
            return None

        # create build File
        bf = SBOMFile()
        bf.path = artifactPath
        bf.relative_path = cfgTarget.target.artifacts[0]
        bf.component = component
        bf.concluded_license = "NOASSERTION"
        bf.copyright_text = "NOASSERTION"

        # add File to Component
        component.files[bf.path] = bf

        # add file to SBOM data
        self.sbom_data.add_file(bf)

        # also set this file as the target component's build product file
        component.target_build_file = bf

        return bf

    # collect a target's source files, add to pending sources queue, and
    # create pending relationship data entry
    # call with:
    #   1) ConfigTarget
    #   2) Component for that target
    #   3) build File for that target
    def collectPendingSourceFiles(self, cfgTarget, component, bf):
        log.dbg("  - collecting source files and adding to pending queue")

        targetIncludesSet = set()

        # walk through target's sources
        for src in cfgTarget.target.sources:
            log.dbg(f"    - add pending source file and relationship for {src.path}")
            # get absolute path if we don't have it
            srcAbspath = src.path
            if not os.path.isabs(src.path):
                srcAbspath = os.path.join(self.cm.paths_source, src.path)

            # check whether it even exists
            if not (os.path.exists(srcAbspath) and os.path.isfile(srcAbspath)):
                log.dbg(f"  - {srcAbspath} does not exist but is referenced in sources for "
                        f"target {component.name}; skipping")
                continue

            # add it to pending source files queue
            self.pendingSources.append(srcAbspath)

            # create relationship data: build file GENERATED_FROM source file
            self.pendingRelationships.append(("file", bf.path, "file", srcAbspath, "GENERATED_FROM"))

            # collect this source file's includes
            if self.cfg.analyzeIncludes and self.compilerPath:
                includes = self.collectIncludes(cfgTarget, component, bf, src)
                for inc in includes:
                    targetIncludesSet.add(inc)

        # make relationships for the overall included files,
        # avoiding duplicates for multiple source files including
        # the same headers
        targetIncludesList = list(targetIncludesSet)
        targetIncludesList.sort()
        for inc in targetIncludesList:
            # add it to pending source files queue
            self.pendingSources.append(inc)

            # create relationship data: build file GENERATED_FROM include file
            self.pendingRelationships.append(("file", bf.path, "file", inc, "GENERATED_FROM"))

    # collect the include files corresponding to this source file
    # call with:
    #   1) ConfigTarget
    #   2) Component for this target
    #   3) build File for this target
    #   4) TargetSource entry for this source file
    # returns: sorted list of include files for this source file
    def collectIncludes(self, cfgTarget, component, bf, src):
        # get the right compile group for this source file
        if len(cfgTarget.target.compileGroups) < (src.compileGroupIndex + 1):
            log.dbg(f"    - {cfgTarget.target.name} has compileGroupIndex {src.compileGroupIndex} "
                    f"but only {len(cfgTarget.target.compileGroups)} found; "
                    "skipping included files search")
            return []
        cg = cfgTarget.target.compileGroups[src.compileGroupIndex]

        # currently only doing C includes
        if cg.language != "C":
            log.dbg(f"    - {cfgTarget.target.name} has compile group language {cg.language} "
                    "but currently only searching includes for C files; "
                    "skipping included files search")
            return []

        srcAbspath = src.path
        if src.path[0] != "/":
            srcAbspath = os.path.join(self.cm.paths_source, src.path)
        return getCIncludes(self.compilerPath, srcAbspath, cg)

    # collect relationships for dependencies of this target Component
    # call with:
    #   1) all ConfigTargets from CodeModel
    #   2) this particular ConfigTarget
    #   3) Component for this Target
    def collectTargetDependencies(self, cfgTargets, cfgTarget, component):
        log.dbg(f"  - collecting target dependencies for {component.name}")

        # walk through target's dependencies
        for dep in cfgTarget.target.dependencies:
            # extract dep name from its id
            depFragments = dep.id.split(":")
            depName = depFragments[0]
            log.dbg(f"    - adding pending relationship for {depName}")

            # create relationship data between dependency components
            self.pendingRelationships.append(("component", component.name, "component", depName, "HAS_PREREQUISITE"))

            # if this is a target with any build artifacts (e.g. non-UTILITY),
            # also create STATIC_LINK relationship for dependency build files,
            # together with this Component's own target build file
            if len(cfgTarget.target.artifacts) == 0:
                continue

            # find the filename for the dependency's build product, using the
            # codemodel (since we might not have created this dependency's
            # Component or File yet)
            depAbspath = ""
            for ct in cfgTargets:
                if ct.name == depName:
                    # skip utility targets
                    if len(ct.target.artifacts) == 0:
                        continue
                    # all targets use the same base_dir, so this works
                    depAbspath = os.path.join(component.base_dir, ct.target.artifacts[0])
                    break
            if depAbspath == "":
                continue

            # create relationship data between build files
            if component.target_build_file:
                self.pendingRelationships.append(("file", component.target_build_file.path, "file", depAbspath, "STATIC_LINK"))

    # walk through pending sources and create corresponding files,
    # assigning them to the appropriate Component
    def walkPendingSources(self):
        log.dbg("walking pending sources")

        for srcAbspath in self.pendingSources:
            # check whether we've already seen it
            if srcAbspath in self.sbom_data.files:
                log.dbg(f"  - {srcAbspath}: already seen")
                continue

            # not yet assigned; figure out which component should own it
            owning_component = self.findOwningComponent(srcAbspath)
            if not owning_component:
                log.dbg(f"  - {srcAbspath}: can't determine which component should own; skipping")
                continue

            # create File and assign it to the Component
            sf = SBOMFile()
            sf.path = srcAbspath
            sf.relative_path = os.path.relpath(srcAbspath, owning_component.base_dir)
            sf.component = owning_component
            sf.concluded_license = "NOASSERTION"
            sf.copyright_text = "NOASSERTION"

            # add File to Component
            owning_component.files[sf.path] = sf

            # add file to SBOM data
            self.sbom_data.add_file(sf)

    # figure out which Component should own the given file based on path
    def findOwningComponent(self, srcAbspath):
        # Check build targets first (most specific)
        for component in self.component_build_targets.values():
            try:
                if os.path.commonpath([srcAbspath, component.base_dir]) == component.base_dir:
                    return component
            except ValueError:
                # Paths don't share a common ancestor
                continue

        # Check SDK
        if self.cfg.includeSDK and self.component_sdk:
            try:
                if os.path.commonpath([srcAbspath, self.component_sdk.base_dir]) == self.component_sdk.base_dir:
                    return self.component_sdk
            except ValueError:
                pass

        # Check app
        if self.component_app:
            try:
                if os.path.commonpath([srcAbspath, self.component_app.base_dir]) == self.component_app.base_dir:
                    return self.component_app
            except ValueError:
                pass

        # Check zephyr
        if self.component_zephyr:
            try:
                if os.path.commonpath([srcAbspath, self.component_zephyr.base_dir]) == self.component_zephyr.base_dir:
                    return self.component_zephyr
            except ValueError:
                pass

        return None

    def _get_document_for_component(self, component_name: str) -> SBomDocument:
        """Get the document that contains the given component."""
        for doc in self.sbom_data.documents.values():
            if component_name in doc.components:
                return doc
        return None

    def _get_document_for_file(self, file_path: str) -> SBomDocument:
        """Get the document that contains the given file."""
        file_obj = self.sbom_data.get_file(file_path)
        if file_obj and file_obj.component:
            return self._get_document_for_component(file_obj.component.name)
        return None

    def _register_external_document_ref(self, from_doc: SBomDocument, to_doc: SBomDocument):
        """Register an external document reference if documents are different."""
        if from_doc and to_doc and from_doc.name != to_doc.name:
            from_doc.add_external_document(to_doc)
            log.dbg(f"  - registered external document ref: {from_doc.name} -> {to_doc.name}")

    # walk through pending relationship data and create relationships
    def walkRelationships(self):
        log.dbg("walking pending relationships")

        for from_type, from_id, to_type, to_id, rln_type in self.pendingRelationships:
            # Get from element
            from_elem = None
            if from_type == "component":
                from_elem = self.sbom_data.get_component(from_id)
            elif from_type == "file":
                from_elem = self.sbom_data.get_file(from_id)

            if not from_elem:
                log.dbg(f"  - skipping relationship: {from_type}:{from_id} not found")
                continue

            # Get to element(s)
            to_elem = None
            if to_type == "component":
                to_elem = self.sbom_data.get_component(to_id)
            elif to_type == "file":
                to_elem = self.sbom_data.get_file(to_id)

            if not to_elem:
                log.dbg(f"  - skipping relationship: {to_type}:{to_id} not found")
                continue

            # Create relationship
            rel = SBOMRelationship()
            rel.from_element = from_elem
            rel.to_elements = [to_elem]
            rel.relationship_type = rln_type

            # Add relationship to from element
            if isinstance(from_elem, SBOMComponent):
                from_elem.relationships.append(rel)
            elif isinstance(from_elem, SBOMFile):
                from_elem.relationships.append(rel)

            # Detect and register cross-document relationships
            from_doc = None
            to_doc = None
            if from_type == "component":
                from_doc = self._get_document_for_component(from_id)
            elif from_type == "file":
                from_doc = self._get_document_for_file(from_id)

            if to_type == "component":
                to_doc = self._get_document_for_component(to_id)
            elif to_type == "file":
                to_doc = self._get_document_for_file(to_id)

            self._register_external_document_ref(from_doc, to_doc)

            log.dbg(f"  - added relationship: {from_type}:{from_id} {rln_type} {to_type}:{to_id}")
