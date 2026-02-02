# Copyright (c) 2020 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
CMake File API Data Models

This module defines Python data classes that mirror the structure of CMake's File API
JSON response. The CMake File API provides detailed information about a project's build
configuration, including targets, source files, compile groups, and dependencies.

The main data model hierarchy is:
- Codemodel: Top-level container with build/source paths and configurations
  - Config: A build configuration (e.g., Debug, Release)
    - ConfigDir: Directory in the build tree
    - ConfigProject: CMake project
    - ConfigTarget: Build target reference with link to full Target data
      - Target: Complete target information (sources, compile settings, dependencies, etc.)

Many classes use an index-based reference system where objects store indices to related
objects (e.g., parentIndex, childIndexes). After parsing, these indices are resolved to
direct object pointers (e.g., parent, children) by linking functions in cmakefileapijson.py.
"""

from enum import Enum


class Codemodel:
    """Top-level CMake codemodel containing project paths and build configurations."""

    def __init__(self):
        super().__init__()

        self.paths_source = ""
        self.paths_build = ""
        self.configurations = []

    def __repr__(self):
        return f"Codemodel: source {self.paths_source}, build {self.paths_build}"


class Config:
    """A build configuration within the codemodel."""

    def __init__(self):
        super().__init__()

        self.name = ""
        self.directories = []
        self.projects = []
        self.configTargets = []

    def __repr__(self):
        if self.name == "":
            return "Config: [no name]"
        else:
            return f"Config: {self.name}"


class ConfigDir:
    """
    A directory in the build tree.
    
    Uses index-based references (parentIndex, childIndexes, projectIndex, targetIndexes)
    that are resolved to object pointers (parent, children, project, targets) after loading.
    """

    def __init__(self):
        super().__init__()

        self.source = ""
        self.build = ""
        self.parentIndex = -1
        self.childIndexes = []
        self.projectIndex = -1
        self.targetIndexes = []
        self.minimumCMakeVersion = ""
        self.hasInstallRule = False

        # Resolved object pointers (calculated from indices after loading)
        self.parent = None
        self.children = []
        self.project = None
        self.targets = []

    def __repr__(self):
        return f"ConfigDir: source {self.source}, build {self.build}"


class ConfigProject:
    """
    A CMake project in the build tree.
    
    Uses index-based references (parentIndex, childIndexes, directoryIndexes, targetIndexes)
    that are resolved to object pointers (parent, children, directories, targets) after loading.
    """

    def __init__(self):
        super().__init__()

        self.name = ""
        self.parentIndex = -1
        self.childIndexes = []
        self.directoryIndexes = []
        self.targetIndexes = []

        # Resolved object pointers (calculated from indices after loading)
        self.parent = None
        self.children = []
        self.directories = []
        self.targets = []

    def __repr__(self):
        return f"ConfigProject: {self.name}"


class ConfigTarget:
    """
    Reference to a build target with metadata and link to full Target data.
    
    Uses index-based references (directoryIndex, projectIndex) that are resolved
    to object pointers (directory, project) after loading.
    """

    def __init__(self):
        super().__init__()

        self.name = ""
        self.id = ""
        self.directoryIndex = -1
        self.projectIndex = -1
        self.jsonFile = ""

        # Full target data loaded from self.jsonFile
        self.target = None

        # Resolved object pointers (calculated from indices after loading)
        self.directory = None
        self.project = None

    def __repr__(self):
        return f"ConfigTarget: {self.name}"


class TargetType(Enum):
    """Types of build targets available in CMake."""
    UNKNOWN = 0
    EXECUTABLE = 1
    STATIC_LIBRARY = 2
    SHARED_LIBRARY = 3
    MODULE_LIBRARY = 4
    OBJECT_LIBRARY = 5
    UTILITY = 6


class TargetInstallDestination:
    """Installation destination path for a target."""

    def __init__(self):
        super().__init__()

        self.path = ""
        self.backtrace = -1

    def __repr__(self):
        return f"TargetInstallDestination: {self.path}"


class TargetCommandFragment:
    """Command fragment for linking or archiving targets."""

    def __init__(self):
        super().__init__()

        self.fragment = ""
        self.role = ""

    def __repr__(self):
        return f"TargetCommandFragment: {self.fragment}"


class TargetDependency:
    """Dependency on another target."""

    def __init__(self):
        super().__init__()

        self.id = ""
        self.backtrace = -1

    def __repr__(self):
        return f"TargetDependency: {self.id}"


class TargetSource:
    """
    A source file in a target.
    
    Uses index-based references (compileGroupIndex, sourceGroupIndex) that are
    resolved to object pointers (compileGroup, sourceGroup) after loading.
    """

    def __init__(self):
        super().__init__()

        self.path = ""
        self.compileGroupIndex = -1
        self.sourceGroupIndex = -1
        self.isGenerated = False
        self.backtrace = -1

        # Resolved object pointers (calculated from indices after loading)
        self.compileGroup = None
        self.sourceGroup = None

    def __repr__(self):
        return f"TargetSource: {self.path}"


class TargetSourceGroup:
    """
    A group of related source files in a target.
    
    Uses index-based references (sourceIndexes) that are resolved to
    object pointers (sources) after loading.
    """

    def __init__(self):
        super().__init__()

        self.name = ""
        self.sourceIndexes = []

        # Resolved object pointers (calculated from indices after loading)
        self.sources = []

    def __repr__(self):
        return f"TargetSourceGroup: {self.name}"


class TargetCompileGroupInclude:
    """Include directory for a compile group."""

    def __init__(self):
        super().__init__()

        self.path = ""
        self.isSystem = False
        self.backtrace = -1

    def __repr__(self):
        return f"TargetCompileGroupInclude: {self.path}"


class TargetCompileGroupPrecompileHeader:
    """Precompiled header for a compile group."""

    def __init__(self):
        super().__init__()

        self.header = ""
        self.backtrace = -1

    def __repr__(self):
        return f"TargetCompileGroupPrecompileHeader: {self.header}"


class TargetCompileGroupDefine:
    """Preprocessor definition for a compile group."""

    def __init__(self):
        super().__init__()

        self.define = ""
        self.backtrace = -1

    def __repr__(self):
        return f"TargetCompileGroupDefine: {self.define}"


class TargetCompileGroup:
    """
    Compilation settings for a group of source files.
    
    Uses index-based references (sourceIndexes) that are resolved to
    object pointers (sources) after loading.
    """

    def __init__(self):
        super().__init__()

        self.sourceIndexes = []
        self.language = ""
        self.compileCommandFragments = []
        self.includes = []
        self.precompileHeaders = []
        self.defines = []
        self.sysroot = ""

        # Resolved object pointers (calculated from indices after loading)
        self.sources = []

    def __repr__(self):
        return f"TargetCompileGroup: {self.sources}"


class TargetBacktraceGraphNode:
    """Node in the backtrace graph for tracking CMake command origins."""

    def __init__(self):
        super().__init__()

        self.file = -1
        self.line = -1
        self.command = -1
        self.parent = -1

    def __repr__(self):
        return f"TargetBacktraceGraphNode: {self.command}"


class Target:
    """Complete build target information including sources, compile settings, and dependencies."""

    def __init__(self):
        super().__init__()

        self.name = ""
        self.id = ""
        self.type = TargetType.UNKNOWN
        self.backtrace = -1
        self.folder = ""
        self.paths_source = ""
        self.paths_build = ""
        self.nameOnDisk = ""
        self.artifacts = []
        self.isGeneratorProvided = False

        # only if install rule is present
        self.install_prefix = ""
        self.install_destinations = []

        # only for executables and shared library targets that link into
        # a runtime binary
        self.link_language = ""
        self.link_commandFragments = []
        self.link_lto = False
        self.link_sysroot = ""

        # only for static library targets
        self.archive_commandFragments = []
        self.archive_lto = False

        # only if the target depends on other targets
        self.dependencies = []

        # corresponds to target's source files
        self.sources = []

        # only if sources are grouped together by source_group() or by default
        self.sourceGroups = []

        # only if target has sources that compile
        self.compileGroups = []

        # graph of backtraces referenced from elsewhere
        self.backtraceGraph_nodes = []
        self.backtraceGraph_commands = []
        self.backtraceGraph_files = []

    def __repr__(self):
        return f"Target: {self.name}"
