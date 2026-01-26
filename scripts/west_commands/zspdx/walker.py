# Copyright (c) 2020-2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import os
import re
from dataclasses import dataclass

import yaml
from west import log
from west.util import WestNotFound, west_topdir

from zspdx.cmakecache import parseCMakeCacheFile
from zspdx.cmakefileapijson import parseReply
from zspdx.getincludes import getCIncludes
from zspdx.model import (
    ComponentPurpose,
    SBOMComponent,
    SBOMData,
    SBOMFile,
    SBOMRelationship,
)


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

        # queue of pending source file paths to create, process and assign
        self.pendingSources = []

        # queue of pending relationship data to create, process and assign
        # Format: (from_type, from_identifier, to_type, to_identifier, relationship_type)
        # Types: "component", "file"
        self.pendingRelationships = []

        # parsed CMake codemodel
        self.cm = None

        # parsed CMake cache dict, once we have the build path
        self.cmakeCache = {}

        # C compiler path from parsed CMake cache
        self.compilerPath = ""

        # SDK install path from parsed CMake cache
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
        # parse CMake cache file and get compiler path
        log.inf("parsing CMake Cache file")
        self.getCacheFile()

        # check if meta file is generated
        if not self.metaFile:
            log.err("CONFIG_BUILD_OUTPUT_META must be enabled to generate spdx files; bailing")
            return None

        # parse codemodel from Walker cfg's build dir
        log.inf("parsing CMake Codemodel files")
        self.cm = self.getCodemodel()
        if not self.cm:
            log.err("could not parse codemodel from CMake API reply; bailing")
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

    # parse cache file and pull out relevant data
    def getCacheFile(self):
        cacheFilePath = os.path.join(self.cfg.buildDir, "CMakeCache.txt")
        self.cmakeCache = parseCMakeCacheFile(cacheFilePath)
        if self.cmakeCache:
            self.compilerPath = self.cmakeCache.get("CMAKE_C_COMPILER", "")
            self.sdkPath = self.cmakeCache.get("ZEPHYR_SDK_INSTALL_DIR", "")
            self.metaFile =  self.cmakeCache.get("KERNEL_META_PATH", "")
            
            # Store build information in SBOM metadata for SPDX 3.0 Build Profile
            build_info = {
                "cmake_compiler": self.cmakeCache.get("CMAKE_C_COMPILER", ""),
                "cmake_cxx_compiler": self.cmakeCache.get("CMAKE_CXX_COMPILER", ""),
                "cmake_build_type": self.cmakeCache.get("CMAKE_BUILD_TYPE", ""),
                "cmake_system_name": self.cmakeCache.get("CMAKE_SYSTEM_NAME", ""),
                "cmake_generator": self.cmakeCache.get("CMAKE_GENERATOR", ""),
                "cmake_system_processor": self.cmakeCache.get("CMAKE_SYSTEM_PROCESSOR", ""),
            }
            # Try to get CMake version (check common keys)
            cmake_version_keys = ["CMAKE_VERSION", "CMAKE_MAJOR_VERSION", "CMAKE_MINOR_VERSION", "CMAKE_PATCH_VERSION"]
            cmake_version = None
            if "CMAKE_VERSION" in self.cmakeCache:
                cmake_version = self.cmakeCache.get("CMAKE_VERSION")
            elif all(k in self.cmakeCache for k in ["CMAKE_MAJOR_VERSION", "CMAKE_MINOR_VERSION", "CMAKE_PATCH_VERSION"]):
                major = self.cmakeCache.get("CMAKE_MAJOR_VERSION", "")
                minor = self.cmakeCache.get("CMAKE_MINOR_VERSION", "")
                patch = self.cmakeCache.get("CMAKE_PATCH_VERSION", "")
                if major and minor:
                    cmake_version = f"{major}.{minor}"
                    if patch:
                        cmake_version += f".{patch}"
            if cmake_version:
                build_info["cmake_version"] = cmake_version
            self.sbom_data.metadata["build_info"] = build_info

    # determine path from build dir to CMake file-based API index file, then
    # parse it and return the Codemodel
    def getCodemodel(self):
        log.dbg("getting codemodel from CMake API reply files")

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

    def setupComponents(self):
        """Set up all SBOM components from meta file and configuration."""
        log.dbg("setting up SBOM components")

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

        self.sbom_data.add_component(component)
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

        self.sbom_data.add_component(component)
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

        self.sbom_data.add_component(component)
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

            self.sbom_data.add_component(component)
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

                # get its source files if build file is found
                if bf:
                    self.collectPendingSourceFiles(cfgTarget, component, bf)
            else:
                log.dbg(f"  - target {cfgTarget.name} has no build artifacts")

            # get its target dependencies
            self.collectTargetDependencies(cfgTargets, cfgTarget, component)

    # build a Component for the given ConfigTarget
    def initConfigTargetComponent(self, cfgTarget):
        log.dbg(f"  - initializing Component for target: {cfgTarget.name}")

        # create target Component
        component = SBOMComponent()
        component.name = cfgTarget.name
        component.base_dir = self.cm.paths_build
        component.declared_license = "NOASSERTION"
        component.copyright_text = "NOASSERTION"

        # add Component to SBOM data
        self.sbom_data.add_component(component)
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

            # Also add to SBOM data relationships if it's a top-level relationship
            # (for now, we add all relationships to the from element)

            log.dbg(f"  - added relationship: {from_type}:{from_id} {rln_type} {to_type}:{to_id}")
