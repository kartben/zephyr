# Copyright (c) 2020-2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
CMake codemodel walker for SBOM generation.

This module provides the Walker class, which traverses the CMake codemodel
and build artifacts to gather information needed to build SBOM documents.
"""

from __future__ import annotations

import os
import re
from dataclasses import dataclass
from typing import TYPE_CHECKING

import yaml
from west import log
from west.util import WestNotFound, west_topdir

import zspdx.spdxids
from zspdx.cmakecache import parseCMakeCacheFile
from zspdx.cmakefileapijson import parseReply
from zspdx.getincludes import getCIncludes
from zspdx.model import (
    DocumentMetadata,
    ElementRegistry,
    ExternalReference,
    PackagePurpose,
    PendingRelationship,
    PendingRelationshipElementType,
    RelationshipType,
    SBOMDocument,
    SBOMFile,
    SBOMPackage,
    SBOMRelationship,
)

if TYPE_CHECKING:
    from zspdx.cmakefileapi import ConfigTarget


@dataclass(eq=True)
class WalkerConfig:
    """
    Configuration data for the Walker.

    Attributes:
        namespacePrefix: Prefix for Document namespaces; should not end with "/".
        buildDir: Location of build directory.
        analyzeIncludes: Whether to analyze for included header files.
        includeSDK: Whether to add an SPDX document for the SDK.
    """

    namespacePrefix: str = ""
    buildDir: str = ""
    analyzeIncludes: bool = False
    includeSDK: bool = False


class Walker:
    """
    Main analysis class for SBOM generation.

    The Walker traverses the CMake codemodel, build files, and corresponding
    source and SDK files, gathering information needed to build the SBOM
    data structures.

    Attributes:
        cfg: WalkerConfig with configuration settings.
        docBuild: Build document (build artifacts).
        docZephyr: Zephyr sources document.
        docApp: Application sources document.
        docSDK: SDK document (optional).
        docModulesExtRefs: Module external references document.
        registry: ElementRegistry for cross-document element tracking.
    """

    def __init__(self, cfg: WalkerConfig) -> None:
        """
        Initialize the Walker with configuration.

        Args:
            cfg: WalkerConfig with settings for this walk.
        """
        super().__init__()

        self.cfg = cfg

        # The various Documents that we will be building
        self.docBuild: SBOMDocument | None = None
        self.docZephyr: SBOMDocument | None = None
        self.docApp: SBOMDocument | None = None
        self.docSDK: SBOMDocument | None = None
        self.docModulesExtRefs: SBOMDocument | None = None

        # Element registry for cross-document lookups
        self.registry = ElementRegistry()

        # Queue of pending source file paths to create, process and assign
        self.pendingSources: list[str] = []

        # Queue of pending relationships to create after elements exist
        self.pendingRelationships: list[PendingRelationship] = []

        # Parsed CMake codemodel
        self.cm = None

        # Parsed CMake cache dict, once we have the build path
        self.cmakeCache: dict[str, str] = {}

        # C compiler path from parsed CMake cache
        self.compilerPath = ""

        # SDK install path from parsed CMake cache
        self.sdkPath = ""

        # Meta file path
        self.metaFile = ""

    def _build_purl(self, url: str, version: str | None = None) -> str | None:
        """
        Build a Package URL (PURL) from a git URL.

        Args:
            url: Git repository URL.
            version: Optional version string (tag or commit).

        Returns:
            PURL string or None if URL doesn't match expected pattern.
        """
        if not url:
            return None

        purl = None
        # Match: <protocol><type>/<namespace>/<package>
        common_git_url_regex = (
            r"((git@|http(s)?:\/\/)(?P<type>[\w\.@]+)(\.\w+)(\/|:))"
            r"(?P<namespace>[\w,\-,\_\/]+)\/(?P<package>[\w,\-,\_]+)"
            r"(.git){0,1}((\/){0,1})$"
        )

        match = re.fullmatch(common_git_url_regex, url)
        if match:
            purl = (
                f'pkg:{match.group("type")}/{match.group("namespace")}/'
                f'{match.group("package")}'
            )

        if purl and version:
            purl += f"@{version}"

        return purl

    def _add_describe_relationship(
        self, doc: SBOMDocument, pkg: SBOMPackage
    ) -> None:
        """
        Add a DESCRIBES relationship from document to package.

        Args:
            doc: The document that describes the package.
            pkg: The package being described.
        """
        rln = SBOMRelationship(
            internal_id=f"rln-{doc.internal_id}-describes-{pkg.internal_id}",
            source=doc,
            target=pkg,
            relationship_type=RelationshipType.DESCRIBES,
        )
        doc.relationships.append(rln)

    def makeDocuments(self) -> bool:
        """
        Primary entry point for document creation.

        Parses the CMake codemodel and builds all SBOM documents.

        Returns:
            True if successful, False otherwise.
        """
        # Parse CMake cache file and get compiler path
        log.inf("parsing CMake Cache file")
        self.getCacheFile()

        # Check if meta file is generated
        if not self.metaFile:
            log.err(
                "CONFIG_BUILD_OUTPUT_META must be enabled to generate spdx files; "
                "bailing"
            )
            return False

        # Parse codemodel from Walker cfg's build dir
        log.inf("parsing CMake Codemodel files")
        self.cm = self.getCodemodel()
        if not self.cm:
            log.err("could not parse codemodel from CMake API reply; bailing")
            return False

        # Set up Documents
        log.inf("setting up SPDX documents")
        retval = self.setupDocuments()
        if not retval:
            return False

        # Walk through targets in codemodel to gather information
        log.inf("walking through targets")
        self.walkTargets()

        # Walk through pending sources and create corresponding files
        log.inf("walking through pending sources files")
        self.walkPendingSources()

        # Walk through pending relationship data and create relationships
        log.inf("walking through pending relationships")
        self.walkRelationships()

        return True

    def getCacheFile(self) -> None:
        """Parse cache file and extract relevant data."""
        cacheFilePath = os.path.join(self.cfg.buildDir, "CMakeCache.txt")
        self.cmakeCache = parseCMakeCacheFile(cacheFilePath)
        if self.cmakeCache:
            self.compilerPath = self.cmakeCache.get("CMAKE_C_COMPILER", "")
            self.sdkPath = self.cmakeCache.get("ZEPHYR_SDK_INSTALL_DIR", "")
            self.metaFile = self.cmakeCache.get("KERNEL_META_PATH", "")

    def getCodemodel(self):
        """
        Get the CMake codemodel from API reply files.

        Returns:
            Parsed Codemodel object or None if not found.
        """
        log.dbg("getting codemodel from CMake API reply files")

        # Make sure the reply directory exists
        cmakeReplyDirPath = os.path.join(
            self.cfg.buildDir, ".cmake", "api", "v1", "reply"
        )
        if not os.path.exists(cmakeReplyDirPath):
            log.err(f"cmake api reply directory {cmakeReplyDirPath} does not exist")
            log.err("was query directory created before cmake build ran?")
            return None
        if not os.path.isdir(cmakeReplyDirPath):
            log.err(
                f"cmake api reply directory {cmakeReplyDirPath} exists but is not "
                "a directory"
            )
            return None

        # Find file with "index" prefix; there should only be one
        indexFilePath = ""
        for f in os.listdir(cmakeReplyDirPath):
            if f.startswith("index"):
                indexFilePath = os.path.join(cmakeReplyDirPath, f)
                break
        if indexFilePath == "":
            log.err(f"cmake api reply index file not found in {cmakeReplyDirPath}")
            return None

        return parseReply(indexFilePath)

    def setupAppDocument(self) -> None:
        """Set up the application sources document."""
        doc = SBOMDocument(
            internal_id="app",
            name="app-sources",
        )
        doc.metadata = DocumentMetadata(
            name="app-sources",
            namespace_prefix=self.cfg.namespacePrefix + "/app",
            doc_ref_id="DocumentRef-app",
        )
        self.docApp = doc
        self.registry.register_document(doc)

        # Also set up app sources package
        pkg = SBOMPackage(
            internal_id="app-sources",
            name="app-sources",
            purpose=PackagePurpose.SOURCE,
            relative_base_dir=self.cm.paths_source,
        )
        doc.add_package(pkg)
        self.registry.register_package(pkg)

        self._add_describe_relationship(doc, pkg)

    def setupBuildDocument(self) -> None:
        """Set up the build document."""
        doc = SBOMDocument(
            internal_id="build",
            name="build",
        )
        doc.metadata = DocumentMetadata(
            name="build",
            namespace_prefix=self.cfg.namespacePrefix + "/build",
            doc_ref_id="DocumentRef-build",
        )
        self.docBuild = doc
        self.registry.register_document(doc)

        # Build packages are created in walkTargets()
        # The DESCRIBES relationship will be with zephyr_final package
        pending = PendingRelationship(
            source_type=PendingRelationshipElementType.DOCUMENT,
            source_identifier="build",
            source_document=doc,
            target_type=PendingRelationshipElementType.PACKAGE_NAME,
            target_identifier="zephyr_final",
            relationship_type=RelationshipType.DESCRIBES,
        )
        self.pendingRelationships.append(pending)

    def setupZephyrDocument(self, zephyr: dict, modules: list) -> bool:
        """
        Set up the Zephyr sources document.

        Args:
            zephyr: Zephyr metadata from meta file.
            modules: List of module metadata from meta file.

        Returns:
            True if successful, False otherwise.
        """
        doc = SBOMDocument(
            internal_id="zephyr",
            name="zephyr-sources",
        )
        doc.metadata = DocumentMetadata(
            name="zephyr-sources",
            namespace_prefix=self.cfg.namespacePrefix + "/zephyr",
            doc_ref_id="DocumentRef-zephyr",
        )
        self.docZephyr = doc
        self.registry.register_document(doc)

        # Get relativeBaseDir from west topdir
        try:
            relativeBaseDir = west_topdir(self.cm.paths_source)
        except WestNotFound:
            log.err(
                "cannot find west_topdir for CMake Codemodel sources path "
                f"{self.cm.paths_source}; bailing"
            )
            return False

        # Set up zephyr sources package
        pkg = SBOMPackage(
            internal_id="zephyr-sources",
            name="zephyr-sources",
            relative_base_dir=relativeBaseDir,
        )

        zephyr_url = zephyr.get("remote", "")
        if zephyr_url:
            pkg.download_url = zephyr_url

        if zephyr.get("revision"):
            pkg.revision = zephyr.get("revision")

        zephyr_tags = zephyr.get("tags", "")
        if zephyr_tags:
            for tag in zephyr_tags:
                version_match = re.fullmatch(r"^v(?P<version>\d+\.\d+\.\d+)$", tag)
                purl = self._build_purl(zephyr_url, tag)

                if purl:
                    pkg.external_references.append(
                        ExternalReference(ref_type="purl", locator=purl)
                    )

                # Extract version from tag once
                if pkg.version == "" and version_match:
                    pkg.version = version_match.group("version")

        if pkg.version:
            cpe = f"cpe:2.3:o:zephyrproject:zephyr:{pkg.version}:-:*:*:*:*:*:*"
            pkg.external_references.append(
                ExternalReference(ref_type="cpe23", locator=cpe)
            )

        doc.add_package(pkg)
        self.registry.register_package(pkg)
        self._add_describe_relationship(doc, pkg)

        # Set up module packages
        for module in modules:
            module_name = module.get("name", None)
            module_path = module.get("path", None)
            module_url = module.get("remote", None)
            module_revision = module.get("revision", None)

            if not module_name:
                log.err("cannot find module name in meta file; bailing")
                return False

            module_pkg = SBOMPackage(
                internal_id=f"{module_name}-sources",
                name=f"{module_name}-sources",
                relative_base_dir=module_path or "",
                purpose=PackagePurpose.SOURCE,
            )

            if module_revision:
                module_pkg.revision = module_revision

            if module_url:
                module_pkg.download_url = module_url

            doc.add_package(module_pkg)
            self.registry.register_package(module_pkg)
            self._add_describe_relationship(doc, module_pkg)

        return True

    def setupSDKDocument(self) -> None:
        """Set up the SDK document."""
        doc = SBOMDocument(
            internal_id="sdk",
            name="sdk",
        )
        doc.metadata = DocumentMetadata(
            name="sdk",
            namespace_prefix=self.cfg.namespacePrefix + "/sdk",
            doc_ref_id="DocumentRef-sdk",
        )
        self.docSDK = doc
        self.registry.register_document(doc)

        # Set up SDK package
        pkg = SBOMPackage(
            internal_id="sdk",
            name="sdk",
            relative_base_dir=self.sdkPath,
        )
        doc.add_package(pkg)
        self.registry.register_package(pkg)
        self._add_describe_relationship(doc, pkg)

    def setupModulesDocument(self, modules: list) -> None:
        """
        Set up the modules external references document.

        Args:
            modules: List of module metadata from meta file.
        """
        doc = SBOMDocument(
            internal_id="modules-deps",
            name="modules-deps",
        )
        doc.metadata = DocumentMetadata(
            name="modules-deps",
            namespace_prefix=self.cfg.namespacePrefix + "/modules-deps",
            doc_ref_id="DocumentRef-modules-deps",
        )
        self.docModulesExtRefs = doc
        self.registry.register_document(doc)

        for module in modules:
            module_name = module.get("name", None)
            module_security = module.get("security", None)

            if not module_name:
                log.err("cannot find module name in meta file; bailing")
                continue

            module_ext_ref = []
            if module_security:
                module_ext_ref = module_security.get("external-references", [])

            pkg = SBOMPackage(
                internal_id=f"{module_name}-deps",
                name=f"{module_name}-deps",
            )

            for ref in module_ext_ref:
                pkg.external_references.append(
                    ExternalReference(ref_type="unknown", locator=ref)
                )

            doc.add_package(pkg)
            self.registry.register_package(pkg)
            self._add_describe_relationship(doc, pkg)

    def setupDocuments(self) -> bool:
        """
        Set up all placeholder documents.

        Returns:
            True if successful, False otherwise.
        """
        log.dbg("setting up placeholder documents")

        self.setupBuildDocument()

        try:
            with open(self.metaFile) as file:
                content = yaml.load(file.read(), yaml.SafeLoader)
                if not self.setupZephyrDocument(content["zephyr"], content["modules"]):
                    return False
        except (FileNotFoundError, yaml.YAMLError):
            log.err(
                "cannot find a valid zephyr.meta required for SPDX generation; bailing"
            )
            return False

        self.setupAppDocument()

        if self.cfg.includeSDK:
            self.setupSDKDocument()

        self.setupModulesDocument(content["modules"])

        return True

    def walkTargets(self) -> None:
        """Walk through targets and gather information."""
        log.dbg("walking targets from codemodel")

        # Assuming just one configuration
        cfgTargets = self.cm.configurations[0].configTargets
        for cfgTarget in cfgTargets:
            # Build the Package for this target
            pkg = self.initConfigTargetPackage(cfgTarget)

            # See whether this target has any build artifacts at all
            if len(cfgTarget.target.artifacts) > 0:
                # Add its build file
                bf = self.addBuildFile(cfgTarget, pkg)
                if pkg.name == "zephyr_final":
                    pkg.purpose = PackagePurpose.APPLICATION
                else:
                    pkg.purpose = PackagePurpose.LIBRARY

                # Get its source files if build file is found
                if bf:
                    self.collectPendingSourceFiles(cfgTarget, pkg, bf)
            else:
                log.dbg(f"  - target {cfgTarget.name} has no build artifacts")

            # Get its target dependencies
            self.collectTargetDependencies(cfgTargets, cfgTarget, pkg)

    def initConfigTargetPackage(self, cfgTarget: ConfigTarget) -> SBOMPackage:
        """
        Create a Package for the given ConfigTarget.

        Args:
            cfgTarget: The CMake target to create a package for.

        Returns:
            The created SBOMPackage.
        """
        log.dbg(f"  - initializing Package for target: {cfgTarget.name}")

        pkg = SBOMPackage(
            internal_id=zspdx.spdxids.convertToSPDXIDSafe(cfgTarget.name),
            name=cfgTarget.name,
            relative_base_dir=self.cm.paths_build,
        )

        # Add Package to build Document
        self.docBuild.add_package(pkg)
        self.registry.register_package(pkg)
        return pkg

    def addBuildFile(
        self, cfgTarget: ConfigTarget, pkg: SBOMPackage
    ) -> SBOMFile | None:
        """
        Create a target's build product File and add it to its Package.

        Args:
            cfgTarget: The CMake target.
            pkg: The Package for that target.

        Returns:
            The created SBOMFile, or None if artifact doesn't exist.
        """
        # Assumes only one artifact in each target
        artifactPath = os.path.join(
            pkg.relative_base_dir, cfgTarget.target.artifacts[0]
        )
        log.dbg(f"  - adding File {artifactPath}")
        log.dbg(f"    - relativeBaseDir: {pkg.relative_base_dir}")
        log.dbg(f"    - artifacts[0]: {cfgTarget.target.artifacts[0]}")

        # Don't create build File if artifact path points to nonexistent file
        if not os.path.exists(artifactPath):
            log.dbg(
                f"  - target {cfgTarget.name} lists build artifact {artifactPath} "
                "but file not found after build; skipping"
            )
            return None

        # Generate unique file ID
        file_id = zspdx.spdxids.getUniqueFileID(
            cfgTarget.target.nameOnDisk, self.docBuild._element_counts
        )

        bf = SBOMFile(
            internal_id=file_id,
            name=cfgTarget.target.nameOnDisk,
            absolute_path=artifactPath,
            relative_path=cfgTarget.target.artifacts[0],
        )

        # Add File to Package
        pkg.add_file(bf)

        # Register file in document and registry
        self.registry.register_file(bf, self.docBuild)

        # Also set this file as the target package's build product file
        pkg.primary_build_file = bf

        return bf

    def collectPendingSourceFiles(
        self, cfgTarget: ConfigTarget, pkg: SBOMPackage, bf: SBOMFile
    ) -> None:
        """
        Collect a target's source files and create pending relationships.

        Args:
            cfgTarget: The CMake target.
            pkg: The Package for that target.
            bf: The build File for that target.
        """
        log.dbg("  - collecting source files and adding to pending queue")

        targetIncludesSet: set[str] = set()

        # Walk through target's sources
        for src in cfgTarget.target.sources:
            log.dbg(f"    - add pending source file and relationship for {src.path}")
            # Get absolute path if we don't have it
            srcAbspath = src.path
            if not os.path.isabs(src.path):
                srcAbspath = os.path.join(self.cm.paths_source, src.path)

            # Check whether it even exists
            if not (os.path.exists(srcAbspath) and os.path.isfile(srcAbspath)):
                log.dbg(
                    f"  - {srcAbspath} does not exist but is referenced in sources "
                    f"for target {pkg.name}; skipping"
                )
                continue

            # Add it to pending source files queue
            self.pendingSources.append(srcAbspath)

            # Create pending relationship
            pending = PendingRelationship(
                source_type=PendingRelationshipElementType.FILE_PATH,
                source_identifier=bf.absolute_path,
                target_type=PendingRelationshipElementType.FILE_PATH,
                target_identifier=srcAbspath,
                relationship_type=RelationshipType.GENERATED_FROM,
            )
            self.pendingRelationships.append(pending)

            # Collect this source file's includes
            if self.cfg.analyzeIncludes and self.compilerPath:
                includes = self.collectIncludes(cfgTarget, pkg, bf, src)
                for inc in includes:
                    targetIncludesSet.add(inc)

        # Make relationships for the overall included files
        targetIncludesList = sorted(targetIncludesSet)
        for inc in targetIncludesList:
            self.pendingSources.append(inc)

            pending = PendingRelationship(
                source_type=PendingRelationshipElementType.FILE_PATH,
                source_identifier=bf.absolute_path,
                target_type=PendingRelationshipElementType.FILE_PATH,
                target_identifier=inc,
                relationship_type=RelationshipType.GENERATED_FROM,
            )
            self.pendingRelationships.append(pending)

    def collectIncludes(
        self, cfgTarget: ConfigTarget, pkg: SBOMPackage, bf: SBOMFile, src
    ) -> list[str]:
        """
        Collect the include files for a source file.

        Args:
            cfgTarget: The CMake target.
            pkg: The Package for this target.
            bf: The build File for this target.
            src: The TargetSource entry for this source file.

        Returns:
            Sorted list of include file paths.
        """
        # Get the right compile group for this source file
        if len(cfgTarget.target.compileGroups) < (src.compileGroupIndex + 1):
            log.dbg(
                f"    - {cfgTarget.target.name} has compileGroupIndex "
                f"{src.compileGroupIndex} but only "
                f"{len(cfgTarget.target.compileGroups)} found; "
                "skipping included files search"
            )
            return []
        cg = cfgTarget.target.compileGroups[src.compileGroupIndex]

        # Currently only doing C includes
        if cg.language != "C":
            log.dbg(
                f"    - {cfgTarget.target.name} has compile group language "
                f"{cg.language} but currently only searching includes for C files; "
                "skipping included files search"
            )
            return []

        srcAbspath = src.path
        if src.path[0] != "/":
            srcAbspath = os.path.join(self.cm.paths_source, src.path)
        return getCIncludes(self.compilerPath, srcAbspath, cg)

    def collectTargetDependencies(
        self, cfgTargets: list, cfgTarget: ConfigTarget, pkg: SBOMPackage
    ) -> None:
        """
        Collect relationships for dependencies of this target Package.

        Args:
            cfgTargets: All ConfigTargets from CodeModel.
            cfgTarget: This particular ConfigTarget.
            pkg: Package for this Target.
        """
        log.dbg(f"  - collecting target dependencies for {pkg.name}")

        # Walk through target's dependencies
        for dep in cfgTarget.target.dependencies:
            # Extract dep name from its id
            depFragments = dep.id.split(":")
            depName = depFragments[0]
            log.dbg(f"    - adding pending relationship for {depName}")

            # Create relationship between dependency packages
            pending = PendingRelationship(
                source_type=PendingRelationshipElementType.PACKAGE_NAME,
                source_identifier=pkg.name,
                target_type=PendingRelationshipElementType.PACKAGE_NAME,
                target_identifier=depName,
                relationship_type=RelationshipType.HAS_PREREQUISITE,
            )
            self.pendingRelationships.append(pending)

            # If this is a target with build artifacts, also create STATIC_LINK
            if len(cfgTarget.target.artifacts) == 0:
                continue

            # Find the dependency's build product file path
            depAbspath = ""
            for ct in cfgTargets:
                if ct.name == depName:
                    # Skip utility targets
                    if len(ct.target.artifacts) == 0:
                        continue
                    depAbspath = os.path.join(
                        pkg.relative_base_dir, ct.target.artifacts[0]
                    )
                    break
            if depAbspath == "":
                continue

            # Create relationship between build files
            pending = PendingRelationship(
                source_type=PendingRelationshipElementType.FILE_PATH,
                source_identifier=pkg.primary_build_file.absolute_path,
                target_type=PendingRelationshipElementType.FILE_PATH,
                target_identifier=depAbspath,
                relationship_type=RelationshipType.STATIC_LINK,
            )
            self.pendingRelationships.append(pending)

    def walkPendingSources(self) -> None:
        """
        Walk through pending sources and create corresponding files.

        Files are assigned to the appropriate Document and Package based
        on their path.
        """
        log.dbg("walking pending sources")

        # Get the single package from each doc (for most docs)
        pkgApp = list(self.docApp.packages.values())[0]
        pkgSDK = None
        if self.cfg.includeSDK:
            pkgSDK = list(self.docSDK.packages.values())[0]

        for srcAbspath in self.pendingSources:
            # Check whether we've already seen it
            if self.registry.find_document_for_file(srcAbspath):
                log.dbg(f"  - {srcAbspath}: already seen")
                continue

            # Figure out where it goes
            srcDoc: SBOMDocument | None = None
            srcPkg: SBOMPackage | None = None

            pkgBuild = self.findBuildPackage(srcAbspath)
            pkgZephyrMatch = self.findZephyrPackage(srcAbspath)

            if pkgBuild:
                log.dbg(
                    f"  - {srcAbspath}: assigning to build document, "
                    f"package {pkgBuild.name}"
                )
                srcDoc = self.docBuild
                srcPkg = pkgBuild
            elif (
                pkgSDK
                and os.path.commonpath([srcAbspath, pkgSDK.relative_base_dir])
                == pkgSDK.relative_base_dir
            ):
                log.dbg(f"  - {srcAbspath}: assigning to sdk document")
                srcDoc = self.docSDK
                srcPkg = pkgSDK
            elif (
                os.path.commonpath([srcAbspath, pkgApp.relative_base_dir])
                == pkgApp.relative_base_dir
            ):
                log.dbg(f"  - {srcAbspath}: assigning to app document")
                srcDoc = self.docApp
                srcPkg = pkgApp
            elif pkgZephyrMatch:
                log.dbg(f"  - {srcAbspath}: assigning to zephyr document")
                srcDoc = self.docZephyr
                srcPkg = pkgZephyrMatch
            else:
                log.dbg(
                    f"  - {srcAbspath}: can't determine which document should own; "
                    "skipping"
                )
                continue

            # Create File and assign it to the Package and Document
            filenameOnly = os.path.split(srcAbspath)[1]
            file_id = zspdx.spdxids.getUniqueFileID(
                filenameOnly, srcDoc._element_counts
            )

            sf = SBOMFile(
                internal_id=file_id,
                name=filenameOnly,
                absolute_path=srcAbspath,
                relative_path=os.path.relpath(srcAbspath, srcPkg.relative_base_dir),
            )

            # Add File to Package
            srcPkg.add_file(sf)

            # Register file in registry
            self.registry.register_file(sf, srcDoc)

    def findPackageFromSrcAbsPath(
        self, document: SBOMDocument, srcAbspath: str
    ) -> SBOMPackage | None:
        """
        Find which Package contains the given file path.

        Multiple packages might "contain" the file path if nested. The one
        with the longest path (most deeply nested) is returned.

        Args:
            document: The document to search in.
            srcAbspath: Absolute path for source filename being searched.

        Returns:
            The matching Package, or None if not found.
        """
        pkgLongestMatch = None
        for pkg in document.packages.values():
            if not pkg.relative_base_dir:
                continue
            if (
                os.path.commonpath([srcAbspath, pkg.relative_base_dir])
                == pkg.relative_base_dir
            ):
                # The package does contain this file; is it the deepest?
                if pkgLongestMatch:
                    if len(pkg.relative_base_dir) > len(
                        pkgLongestMatch.relative_base_dir
                    ):
                        pkgLongestMatch = pkg
                else:
                    pkgLongestMatch = pkg

        return pkgLongestMatch

    def findBuildPackage(self, srcAbspath: str) -> SBOMPackage | None:
        """Find which build Package contains the given file path."""
        return self.findPackageFromSrcAbsPath(self.docBuild, srcAbspath)

    def findZephyrPackage(self, srcAbspath: str) -> SBOMPackage | None:
        """Find which Zephyr Package contains the given file path."""
        return self.findPackageFromSrcAbsPath(self.docZephyr, srcAbspath)

    def walkRelationships(self) -> None:
        """
        Walk through pending relationships and create actual relationships.

        Converts PendingRelationship entries to SBOMRelationship objects
        and attaches them to the appropriate elements.
        """
        for pending in self.pendingRelationships:
            # Get left side of relationship
            source_result = self.getRelationshipLeft(pending)
            if source_result is None:
                continue
            source_element, source_doc, source_rlns = source_result

            # Get right side of relationship
            target_result = self.getRelationshipRight(pending, source_doc)
            if target_result is None:
                continue
            target_element, target_doc = target_result

            # Create the relationship
            rln = SBOMRelationship(
                internal_id=f"rln-{source_element.internal_id}-{target_element.internal_id}",
                source=source_element,
                target=target_element,
                relationship_type=pending.relationship_type,
            )

            # Set external document reference if needed
            if target_doc and target_doc != source_doc:
                rln.external_document = target_doc
                source_doc.external_documents.add(target_doc)

            source_rlns.append(rln)
            log.dbg(
                f"  - adding relationship to {source_doc.name}: "
                f"{source_element.internal_id} {pending.relationship_type.name} "
                f"{target_element.internal_id}"
            )

    def getRelationshipLeft(
        self, pending: PendingRelationship
    ) -> tuple | None:
        """
        Get owner (left side) element of relationship.

        Args:
            pending: The pending relationship data.

        Returns:
            Tuple of (element, document, relationships_list) or None if not found.
        """
        if pending.source_type == PendingRelationshipElementType.FILE_PATH:
            result = self.registry.find_file_by_path(pending.source_identifier)
            if not result:
                log.dbg(
                    f"  - searching for relationship, can't find file "
                    f"{pending.source_identifier}; skipping"
                )
                return None
            sf, doc = result
            return (sf, doc, sf.relationships)

        elif pending.source_type == PendingRelationshipElementType.PACKAGE_NAME:
            # For package names, must be in docBuild
            for pkg in self.docBuild.packages.values():
                if pkg.name == pending.source_identifier:
                    return (pkg, self.docBuild, pkg.relationships)
            log.dbg(
                f"  - searching for relationship for target "
                f"{pending.source_identifier}, target not found; skipping"
            )
            return None

        elif pending.source_type == PendingRelationshipElementType.DOCUMENT:
            doc = pending.source_document
            if not doc:
                doc = self.registry.find_document_by_id(pending.source_identifier)
            if not doc:
                log.dbg(
                    f"  - searching for relationship, can't find document "
                    f"{pending.source_identifier}; skipping"
                )
                return None
            return (doc, doc, doc.relationships)

        else:
            log.dbg(f"  - unknown relationship source type {pending.source_type}")
            return None

    def getRelationshipRight(
        self, pending: PendingRelationship, source_doc: SBOMDocument
    ) -> tuple | None:
        """
        Get other (right side) element of relationship.

        Args:
            pending: The pending relationship data.
            source_doc: The document containing the source element.

        Returns:
            Tuple of (element, document) or None if not found.
        """
        if pending.target_type == PendingRelationshipElementType.FILE_PATH:
            result = self.registry.find_file_by_path(pending.target_identifier)
            if not result:
                log.dbg(
                    f"  - searching for relationship, can't find file "
                    f"{pending.target_identifier}; skipping"
                )
                return None
            return result

        elif pending.target_type == PendingRelationshipElementType.PACKAGE_NAME:
            # For package names, must be in docBuild
            for pkg in self.docBuild.packages.values():
                if pkg.name == pending.target_identifier:
                    return (pkg, self.docBuild)
            log.dbg(
                f"  - searching for relationship for target "
                f"{pending.target_identifier}, target not found; skipping"
            )
            return None

        elif pending.target_type == PendingRelationshipElementType.PACKAGE_ID:
            # Look up package by internal ID
            pkg = self.registry.find_package_by_name(pending.target_identifier)
            if pkg:
                # Find which document contains this package
                for doc in self.registry.documents.values():
                    if pending.target_identifier in doc.packages:
                        return (pkg, doc)
            return None

        else:
            log.dbg(f"  - unknown relationship target type {pending.target_type}")
            return None
