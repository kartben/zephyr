# Copyright (c) 2020 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
CMake File API JSON Parser

This module parses JSON files from CMake's File API and converts them into
Python data structures defined in cmakefileapi.py.

The CMake File API generates JSON files in the build directory under
.cmake/api/v1/reply/ containing detailed build information. This parser:

1. Reads the index file to locate the codemodel file
2. Parses the codemodel and all referenced target JSON files
3. Resolves index-based references to create direct object pointers

Main entry point: parseReply(replyIndexPath)
"""

import json
import os

from west import log

import zspdx.cmakefileapi

# CMake File API constants
CMAKE_REPLY_FIELD = "reply"
CMAKE_CODEMODEL_FIELD = "codemodel-v2"
CMAKE_JSON_FILE_FIELD = "jsonFile"
CMAKE_KIND_CODEMODEL = "codemodel"
CMAKE_VERSION_MAJOR_EXPECTED = 2


def parseReply(replyIndexPath):
    """
    Parse a CMake File API reply index file and load the codemodel.

    Args:
        replyIndexPath: Path to the index-*.json file in .cmake/api/v1/reply/

    Returns:
        Codemodel object with full build information, or None on error
    """
    replyDir, _ = os.path.split(replyIndexPath)

    # First find the codemodel reply file from the index
    try:
        with open(replyIndexPath) as indexFile:
            js = json.load(indexFile)

            # Get reply object
            reply_dict = js.get(CMAKE_REPLY_FIELD, {})
            if reply_dict == {}:
                log.err(f'no "{CMAKE_REPLY_FIELD}" field found in index file')
                return None

            # Get codemodel object
            cm_dict = reply_dict.get(CMAKE_CODEMODEL_FIELD, {})
            if cm_dict == {}:
                log.err(f'no "{CMAKE_CODEMODEL_FIELD}" field found in "{CMAKE_REPLY_FIELD}" object in index file')
                return None

            # Get codemodel filename
            jsonFile = cm_dict.get(CMAKE_JSON_FILE_FIELD, "")
            if jsonFile == "":
                log.err(f'no "{CMAKE_JSON_FILE_FIELD}" field found in "{CMAKE_CODEMODEL_FIELD}" object in index file')
                return None

            return parseCodemodel(replyDir, jsonFile)

    except OSError as e:
        log.err(f"Error loading {replyIndexPath}: {str(e)}")
        return None
    except json.decoder.JSONDecodeError as e:
        log.err(f"Error parsing JSON in {replyIndexPath}: {str(e)}")
        return None


def parseCodemodel(replyDir, codemodelFile):
    """
    Parse the main codemodel JSON file.

    Args:
        replyDir: Directory containing the reply JSON files
        codemodelFile: Filename of the codemodel JSON file

    Returns:
        Codemodel object with all configurations and targets, or None on error
    """
    codemodelPath = os.path.join(replyDir, codemodelFile)

    try:
        with open(codemodelPath) as cmFile:
            js = json.load(cmFile)

            cm = zspdx.cmakefileapi.Codemodel()

            # Verify kind and version for correctness
            kind = js.get("kind", "")
            if kind != CMAKE_KIND_CODEMODEL:
                log.err(f'Error loading CMake API reply: expected "kind":"{CMAKE_KIND_CODEMODEL}" '
                        f'in {codemodelPath}, got {kind}')
                return None

            version = js.get("version", {})
            versionMajor = version.get("major", -1)
            if versionMajor != CMAKE_VERSION_MAJOR_EXPECTED:
                if versionMajor == -1:
                    log.err(f"Error loading CMake API reply: expected major version {CMAKE_VERSION_MAJOR_EXPECTED} "
                            f"in {codemodelPath}, no version found")
                    return None
                log.err(f"Error loading CMake API reply: expected major version {CMAKE_VERSION_MAJOR_EXPECTED} "
                        f"in {codemodelPath}, got {versionMajor}")
                return None

            # get paths
            paths_dict = js.get("paths", {})
            cm.paths_source = paths_dict.get("source", "")
            cm.paths_build = paths_dict.get("build", "")

            # Parse each configuration
            configs_arr = js.get("configurations", [])
            for cfg_dict in configs_arr:
                cfg = parseConfig(cfg_dict, replyDir)
                if cfg:
                    cm.configurations.append(cfg)

            # After parsing, link all index-based references to direct object pointers
            linkCodemodel(cm)

            return cm

    except OSError as e:
        log.err(f"Error loading {codemodelPath}: {str(e)}")
        return None
    except json.decoder.JSONDecodeError as e:
        log.err(f"Error parsing JSON in {codemodelPath}: {str(e)}")
        return None


def parseConfig(cfg_dict, replyDir):
    """
    Parse a configuration from the codemodel.

    Args:
        cfg_dict: Dictionary containing configuration data
        replyDir: Directory containing target JSON files

    Returns:
        Config object with directories, projects, and targets
    """
    cfg = zspdx.cmakefileapi.Config()
    cfg.name = cfg_dict.get("name", "")

    # Parse and add each directory
    dirs_arr = cfg_dict.get("directories", [])
    for dir_dict in dirs_arr:
        if dir_dict != {}:
            cfgdir = zspdx.cmakefileapi.ConfigDir()
            cfgdir.source = dir_dict.get("source", "")
            cfgdir.build = dir_dict.get("build", "")
            cfgdir.parentIndex = dir_dict.get("parentIndex", -1)
            cfgdir.childIndexes = dir_dict.get("childIndexes", [])
            cfgdir.projectIndex = dir_dict.get("projectIndex", -1)
            cfgdir.targetIndexes = dir_dict.get("targetIndexes", [])
            minCMakeVer_dict = dir_dict.get("minimumCMakeVersion", {})
            cfgdir.minimumCMakeVersion = minCMakeVer_dict.get("string", "")
            cfgdir.hasInstallRule = dir_dict.get("hasInstallRule", False)
            cfg.directories.append(cfgdir)

    # Parse and add each project
    projects_arr = cfg_dict.get("projects", [])
    for prj_dict in projects_arr:
        if prj_dict != {}:
            prj = zspdx.cmakefileapi.ConfigProject()
            prj.name = prj_dict.get("name", "")
            prj.parentIndex = prj_dict.get("parentIndex", -1)
            prj.childIndexes = prj_dict.get("childIndexes", [])
            prj.directoryIndexes = prj_dict.get("directoryIndexes", [])
            prj.targetIndexes = prj_dict.get("targetIndexes", [])
            cfg.projects.append(prj)

    # Parse and add each target
    cfgTargets_arr = cfg_dict.get("targets", [])
    for cfgTarget_dict in cfgTargets_arr:
        if cfgTarget_dict != {}:
            cfgTarget = zspdx.cmakefileapi.ConfigTarget()
            cfgTarget.name = cfgTarget_dict.get("name", "")
            cfgTarget.id = cfgTarget_dict.get("id", "")
            cfgTarget.directoryIndex = cfgTarget_dict.get("directoryIndex", -1)
            cfgTarget.projectIndex = cfgTarget_dict.get("projectIndex", -1)
            cfgTarget.jsonFile = cfgTarget_dict.get("jsonFile", "")

            if cfgTarget.jsonFile != "":
                cfgTarget.target = parseTarget(os.path.join(replyDir, cfgTarget.jsonFile))
            else:
                cfgTarget.target = None

            cfg.configTargets.append(cfgTarget)

    return cfg


def parseTarget(targetPath):
    """
    Parse a target JSON file.

    Args:
        targetPath: Path to the target-*.json file

    Returns:
        Target object with full target information, or None on error
    """
    try:
        with open(targetPath) as targetFile:
            js = json.load(targetFile)

            target = zspdx.cmakefileapi.Target()

            target.name = js.get("name", "")
            target.id = js.get("id", "")
            target.type = parseTargetType(js.get("type", "UNKNOWN"))
            target.backtrace = js.get("backtrace", -1)
            target.folder = js.get("folder", "")

            # get paths
            paths_dict = js.get("paths", {})
            target.paths_source = paths_dict.get("source", "")
            target.paths_build = paths_dict.get("build", "")

            target.nameOnDisk = js.get("nameOnDisk", "")

            # parse artifacts if present
            artifacts_arr = js.get("artifacts", [])
            target.artifacts = []
            for artifact_dict in artifacts_arr:
                artifact_path = artifact_dict.get("path", "")
                if artifact_path != "":
                    target.artifacts.append(artifact_path)

            target.isGeneratorProvided = js.get("isGeneratorProvided", False)

            # Parse target subsections
            parseTargetInstall(target, js)
            parseTargetLink(target, js)
            parseTargetArchive(target, js)
            parseTargetDependencies(target, js)
            parseTargetSources(target, js)
            parseTargetSourceGroups(target, js)
            parseTargetCompileGroups(target, js)
            parseTargetBacktraceGraph(target, js)

            return target

    except OSError as e:
        log.err(f"Error loading {targetPath}: {str(e)}")
        return None
    except json.decoder.JSONDecodeError as e:
        log.err(f"Error parsing JSON in {targetPath}: {str(e)}")
        return None


def parseTargetType(targetType):
    """
    Convert target type string to TargetType enum.

    Args:
        targetType: Target type string from JSON

    Returns:
        TargetType enum value
    """
    return {
        "EXECUTABLE": zspdx.cmakefileapi.TargetType.EXECUTABLE,
        "STATIC_LIBRARY": zspdx.cmakefileapi.TargetType.STATIC_LIBRARY,
        "SHARED_LIBRARY": zspdx.cmakefileapi.TargetType.SHARED_LIBRARY,
        "MODULE_LIBRARY": zspdx.cmakefileapi.TargetType.MODULE_LIBRARY,
        "OBJECT_LIBRARY": zspdx.cmakefileapi.TargetType.OBJECT_LIBRARY,
        "UTILITY": zspdx.cmakefileapi.TargetType.UTILITY,
    }.get(targetType, zspdx.cmakefileapi.TargetType.UNKNOWN)


def _parseCommandFragments(fragments_arr):
    """
    Helper function to parse command fragments array.

    Args:
        fragments_arr: Array of fragment dictionaries

    Returns:
        List of TargetCommandFragment objects
    """
    fragments = []
    for fragment_dict in fragments_arr:
        fragment = zspdx.cmakefileapi.TargetCommandFragment()
        fragment.fragment = fragment_dict.get("fragment", "")
        fragment.role = fragment_dict.get("role", "")
        fragments.append(fragment)
    return fragments


def parseTargetInstall(target, js):
    """Parse target installation information."""
    install_dict = js.get("install", {})
    if install_dict == {}:
        return
    prefix_dict = install_dict.get("prefix", {})
    target.install_prefix = prefix_dict.get("path", "")

    destinations_arr = install_dict.get("destinations", [])
    for destination_dict in destinations_arr:
        dest = zspdx.cmakefileapi.TargetInstallDestination()
        dest.path = destination_dict.get("path", "")
        dest.backtrace = destination_dict.get("backtrace", -1)
        target.install_destinations.append(dest)


def parseTargetLink(target, js):
    """Parse target linking information."""
    link_dict = js.get("link", {})
    if link_dict == {}:
        return
    target.link_language = link_dict.get("language", {})
    target.link_lto = link_dict.get("lto", False)
    sysroot_dict = link_dict.get("sysroot", {})
    target.link_sysroot = sysroot_dict.get("path", "")

    # Use helper function to parse command fragments
    fragments_arr = link_dict.get("commandFragments", [])
    target.link_commandFragments = _parseCommandFragments(fragments_arr)


def parseTargetArchive(target, js):
    """Parse target archive information (for static libraries)."""
    archive_dict = js.get("archive", {})
    if archive_dict == {}:
        return
    target.archive_lto = archive_dict.get("lto", False)

    # Use helper function to parse command fragments
    fragments_arr = archive_dict.get("commandFragments", [])
    target.archive_commandFragments = _parseCommandFragments(fragments_arr)


def parseTargetDependencies(target, js):
    """Parse target dependencies."""
    dependencies_arr = js.get("dependencies", [])
    for dependency_dict in dependencies_arr:
        dep = zspdx.cmakefileapi.TargetDependency()
        dep.id = dependency_dict.get("id", "")
        dep.backtrace = dependency_dict.get("backtrace", -1)
        target.dependencies.append(dep)


def parseTargetSources(target, js):
    """Parse target source files."""
    sources_arr = js.get("sources", [])
    for source_dict in sources_arr:
        src = zspdx.cmakefileapi.TargetSource()
        src.path = source_dict.get("path", "")
        src.compileGroupIndex = source_dict.get("compileGroupIndex", -1)
        src.sourceGroupIndex = source_dict.get("sourceGroupIndex", -1)
        src.isGenerated = source_dict.get("isGenerated", False)
        src.backtrace = source_dict.get("backtrace", -1)
        target.sources.append(src)


def parseTargetSourceGroups(target, js):
    """Parse target source groups."""
    sourceGroups_arr = js.get("sourceGroups", [])
    for sourceGroup_dict in sourceGroups_arr:
        srcgrp = zspdx.cmakefileapi.TargetSourceGroup()
        srcgrp.name = sourceGroup_dict.get("name", "")
        srcgrp.sourceIndexes = sourceGroup_dict.get("sourceIndexes", [])
        target.sourceGroups.append(srcgrp)


def _parseCompileGroupIncludes(includes_arr):
    """
    Helper function to parse compile group includes.

    Args:
        includes_arr: Array of include dictionaries

    Returns:
        List of TargetCompileGroupInclude objects
    """
    includes = []
    for include_dict in includes_arr:
        grpInclude = zspdx.cmakefileapi.TargetCompileGroupInclude()
        grpInclude.path = include_dict.get("path", "")
        grpInclude.isSystem = include_dict.get("isSystem", False)
        grpInclude.backtrace = include_dict.get("backtrace", -1)
        includes.append(grpInclude)
    return includes


def _parseCompileGroupPrecompileHeaders(headers_arr):
    """
    Helper function to parse compile group precompiled headers.

    Args:
        headers_arr: Array of precompiled header dictionaries

    Returns:
        List of TargetCompileGroupPrecompileHeader objects
    """
    headers = []
    for header_dict in headers_arr:
        grpHeader = zspdx.cmakefileapi.TargetCompileGroupPrecompileHeader()
        grpHeader.header = header_dict.get("header", "")
        grpHeader.backtrace = header_dict.get("backtrace", -1)
        headers.append(grpHeader)
    return headers


def _parseCompileGroupDefines(defines_arr):
    """
    Helper function to parse compile group preprocessor defines.

    Args:
        defines_arr: Array of define dictionaries

    Returns:
        List of TargetCompileGroupDefine objects
    """
    defines = []
    for define_dict in defines_arr:
        grpDefine = zspdx.cmakefileapi.TargetCompileGroupDefine()
        grpDefine.define = define_dict.get("define", "")
        grpDefine.backtrace = define_dict.get("backtrace", -1)
        defines.append(grpDefine)
    return defines


def parseTargetCompileGroups(target, js):
    """Parse target compile groups with includes, defines, and headers."""
    compileGroups_arr = js.get("compileGroups", [])
    for compileGroup_dict in compileGroups_arr:
        cmpgrp = zspdx.cmakefileapi.TargetCompileGroup()
        cmpgrp.sourceIndexes = compileGroup_dict.get("sourceIndexes", [])
        cmpgrp.language = compileGroup_dict.get("language", "")
        cmpgrp.sysroot = compileGroup_dict.get("sysroot", "")

        # Parse compile command fragments
        commandFragments_arr = compileGroup_dict.get("compileCommandFragments", [])
        for commandFragment_dict in commandFragments_arr:
            fragment = commandFragment_dict.get("fragment", "")
            if fragment != "":
                cmpgrp.compileCommandFragments.append(fragment)

        # Use helper functions to parse includes, headers, and defines
        includes_arr = compileGroup_dict.get("includes", [])
        cmpgrp.includes = _parseCompileGroupIncludes(includes_arr)

        precompileHeaders_arr = compileGroup_dict.get("precompileHeaders", [])
        cmpgrp.precompileHeaders = _parseCompileGroupPrecompileHeaders(precompileHeaders_arr)

        defines_arr = compileGroup_dict.get("defines", [])
        cmpgrp.defines = _parseCompileGroupDefines(defines_arr)

        target.compileGroups.append(cmpgrp)


def parseTargetBacktraceGraph(target, js):
    """Parse target backtrace graph for tracking CMake command origins."""
    backtraceGraph_dict = js.get("backtraceGraph", {})
    if backtraceGraph_dict == {}:
        return
    target.backtraceGraph_commands = backtraceGraph_dict.get("commands", [])
    target.backtraceGraph_files = backtraceGraph_dict.get("files", [])

    nodes_arr = backtraceGraph_dict.get("nodes", [])
    for node_dict in nodes_arr:
        node = zspdx.cmakefileapi.TargetBacktraceGraphNode()
        node.file = node_dict.get("file", -1)
        node.line = node_dict.get("line", -1)
        node.command = node_dict.get("command", -1)
        node.parent = node_dict.get("parent", -1)
        target.backtraceGraph_nodes.append(node)


# =============================================================================
# Linking Functions - Resolve index-based references to direct object pointers
# =============================================================================


def linkCodemodel(cm):
    """
    Create direct object pointers for all Configs in Codemodel.

    Args:
        cm: Codemodel object with configurations
    """
    for cfg in cm.configurations:
        linkConfig(cfg)


def linkConfig(cfg):
    """
    Create direct object pointers for all contents of Config.

    Args:
        cfg: Config object with directories, projects, and targets
    """
    for cfgDir in cfg.directories:
        linkConfigDir(cfg, cfgDir)
    for cfgPrj in cfg.projects:
        linkConfigProject(cfg, cfgPrj)
    for cfgTarget in cfg.configTargets:
        linkConfigTarget(cfg, cfgTarget)


def linkConfigDir(cfg, cfgDir):
    """
    Create direct object pointers for ConfigDir indices.

    Args:
        cfg: Config object containing the directory
        cfgDir: ConfigDir object with index-based references
    """
    if cfgDir.parentIndex == -1:
        cfgDir.parent = None
    else:
        cfgDir.parent = cfg.directories[cfgDir.parentIndex]

    if cfgDir.projectIndex == -1:
        cfgDir.project = None
    else:
        cfgDir.project = cfg.projects[cfgDir.projectIndex]

    cfgDir.children = []
    for childIndex in cfgDir.childIndexes:
        cfgDir.children.append(cfg.directories[childIndex])

    cfgDir.targets = []
    for targetIndex in cfgDir.targetIndexes:
        cfgDir.targets.append(cfg.configTargets[targetIndex])


def linkConfigProject(cfg, cfgPrj):
    """
    Create direct object pointers for ConfigProject indices.

    Args:
        cfg: Config object containing the project
        cfgPrj: ConfigProject object with index-based references
    """
    if cfgPrj.parentIndex == -1:
        cfgPrj.parent = None
    else:
        cfgPrj.parent = cfg.projects[cfgPrj.parentIndex]

    cfgPrj.children = []
    for childIndex in cfgPrj.childIndexes:
        cfgPrj.children.append(cfg.projects[childIndex])

    cfgPrj.directories = []
    for dirIndex in cfgPrj.directoryIndexes:
        cfgPrj.directories.append(cfg.directories[dirIndex])

    cfgPrj.targets = []
    for targetIndex in cfgPrj.targetIndexes:
        cfgPrj.targets.append(cfg.configTargets[targetIndex])


def linkConfigTarget(cfg, cfgTarget):
    """
    Create direct object pointers for ConfigTarget indices.

    Args:
        cfg: Config object containing the target
        cfgTarget: ConfigTarget object with index-based references
    """
    if cfgTarget.directoryIndex == -1:
        cfgTarget.directory = None
    else:
        cfgTarget.directory = cfg.directories[cfgTarget.directoryIndex]

    if cfgTarget.projectIndex == -1:
        cfgTarget.project = None
    else:
        cfgTarget.project = cfg.projects[cfgTarget.projectIndex]

    # Link target's sources and source groups
    for ts in cfgTarget.target.sources:
        linkTargetSource(cfgTarget.target, ts)
    for tsg in cfgTarget.target.sourceGroups:
        linkTargetSourceGroup(cfgTarget.target, tsg)
    for tcg in cfgTarget.target.compileGroups:
        linkTargetCompileGroup(cfgTarget.target, tcg)


def linkTargetSource(target, targetSrc):
    """
    Create direct object pointers for TargetSource indices.

    Args:
        target: Target object containing the source
        targetSrc: TargetSource object with index-based references
    """
    if targetSrc.compileGroupIndex == -1:
        targetSrc.compileGroup = None
    else:
        targetSrc.compileGroup = target.compileGroups[targetSrc.compileGroupIndex]

    if targetSrc.sourceGroupIndex == -1:
        targetSrc.sourceGroup = None
    else:
        targetSrc.sourceGroup = target.sourceGroups[targetSrc.sourceGroupIndex]


def linkTargetSourceGroup(target, targetSrcGrp):
    """
    Create direct object pointers for TargetSourceGroup indices.

    Args:
        target: Target object containing the source group
        targetSrcGrp: TargetSourceGroup object with index-based references
    """
    targetSrcGrp.sources = []
    for srcIndex in targetSrcGrp.sourceIndexes:
        targetSrcGrp.sources.append(target.sources[srcIndex])


def linkTargetCompileGroup(target, targetCmpGrp):
    """
    Create direct object pointers for TargetCompileGroup indices.

    Args:
        target: Target object containing the compile group
        targetCmpGrp: TargetCompileGroup object with index-based references
    """
    targetCmpGrp.sources = []
    for srcIndex in targetCmpGrp.sourceIndexes:
        targetCmpGrp.sources.append(target.sources[srcIndex])
