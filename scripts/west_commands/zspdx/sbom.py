# Copyright (c) 2020, 2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
SBOM generation orchestration module.

This module provides the main entry point for SBOM generation, coordinating
the Walker, Scanner, and Serializer components.
"""

from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path

from west import log

from zspdx.scanner import ScannerConfig, scanDocument
from zspdx.serializers import SerializerConfig, SPDX2TagValueSerializer
from zspdx.version import SPDX_VERSION_2_3
from zspdx.walker import Walker, WalkerConfig


@dataclass(eq=True)
class SBOMConfig:
    """
    Configuration settings for SBOM generation.

    These settings are passed along to the various SBOM maker subcomponents.

    Attributes:
        namespacePrefix: Prefix for Document namespaces; should not end with "/".
        buildDir: Location of build directory.
        spdxDir: Location of SPDX document output directory.
        spdxVersion: SPDX specification version to use.
        analyzeIncludes: Should also analyze for included header files?
        includeSDK: Should also add an SPDX document for the SDK?
    """

    namespacePrefix: str = ""
    buildDir: str = ""
    spdxDir: str = ""
    spdxVersion: str = str(SPDX_VERSION_2_3)
    analyzeIncludes: bool = False
    includeSDK: bool = False


def setupCmakeQuery(build_dir: str) -> bool:
    """
    Create CMake file-based API directories and query file.

    This must be called before running the CMake build so that CMake
    generates the codemodel reply files.

    Args:
        build_dir: Build directory path.

    Returns:
        True if successful, False otherwise.
    """
    # Check that query dir exists as a directory, or else create it
    cmakeApiDirPath = os.path.join(build_dir, ".cmake", "api", "v1", "query")
    if os.path.exists(cmakeApiDirPath):
        if not os.path.isdir(cmakeApiDirPath):
            log.err(f"cmake api query directory {cmakeApiDirPath} exists and is not a directory")
            return False
        # Directory exists, we're good
    else:
        # Create the directory
        os.makedirs(cmakeApiDirPath, exist_ok=False)

    # Check that codemodel-v2 exists as a file, or else create it
    queryFilePath = os.path.join(cmakeApiDirPath, "codemodel-v2")
    if os.path.exists(queryFilePath):
        if not os.path.isfile(queryFilePath):
            log.err(f"cmake api query file {queryFilePath} exists and is not a directory")
            return False
        # File exists, we're good
        return True
    else:
        # File doesn't exist, let's create an empty file
        with open(queryFilePath, "w"):
            pass
        return True


def makeSPDX(cfg: SBOMConfig) -> bool:
    """
    Main entry point for SBOM generation.

    This function orchestrates the complete SBOM generation process:
    1. Walker: Parses CMake codemodel and builds document structure
    2. Scanner: Analyzes files for licenses and hashes
    3. Serializer: Writes documents to SPDX format

    Args:
        cfg: SBOMConfig with settings for generation.

    Returns:
        True if successful, False otherwise.
    """
    # Report any odd configuration settings
    if cfg.analyzeIncludes and not cfg.includeSDK:
        log.wrn("config: requested to analyze includes but not to generate SDK SPDX document;")
        log.wrn("config: will proceed but will discard detected includes for SDK header files")

    # Set up walker configuration
    walkerCfg = WalkerConfig()
    walkerCfg.namespacePrefix = cfg.namespacePrefix
    walkerCfg.buildDir = cfg.buildDir
    walkerCfg.analyzeIncludes = cfg.analyzeIncludes
    walkerCfg.includeSDK = cfg.includeSDK

    # Make and run the walker
    w = Walker(walkerCfg)
    retval = w.makeDocuments()
    if not retval:
        log.err("SPDX walker failed; bailing")
        return False

    # Set up scanner configuration
    scannerCfg = ScannerConfig()

    # Scan each document from walker
    if cfg.includeSDK:
        scanDocument(scannerCfg, w.docSDK)
    scanDocument(scannerCfg, w.docApp)
    scanDocument(scannerCfg, w.docZephyr)
    scanDocument(scannerCfg, w.docBuild)

    # Set up serializer
    serializerConfig = SerializerConfig(version=cfg.spdxVersion)
    serializer = SPDX2TagValueSerializer(serializerConfig)

    # Ensure output directory exists
    spdxDir = Path(cfg.spdxDir)
    spdxDir.mkdir(parents=True, exist_ok=True)

    # Write each document, in this particular order so that the
    # hashes for external references are calculated

    # Write SDK document, if we made one
    if cfg.includeSDK:
        try:
            serializer.serialize_to_file(w.docSDK, spdxDir / "sdk.spdx")
        except Exception as e:
            log.err(f"SPDX writer failed for SDK document: {e}; bailing")
            return False

    # Write app document
    try:
        serializer.serialize_to_file(w.docApp, spdxDir / "app.spdx")
    except Exception as e:
        log.err(f"SPDX writer failed for app document: {e}; bailing")
        return False

    # Write zephyr document
    try:
        serializer.serialize_to_file(w.docZephyr, spdxDir / "zephyr.spdx")
    except Exception as e:
        log.err(f"SPDX writer failed for zephyr document: {e}; bailing")
        return False

    # Write build document
    try:
        serializer.serialize_to_file(w.docBuild, spdxDir / "build.spdx")
    except Exception as e:
        log.err(f"SPDX writer failed for build document: {e}; bailing")
        return False

    # Write modules document
    try:
        serializer.serialize_to_file(w.docModulesExtRefs, spdxDir / "modules-deps.spdx")
    except Exception as e:
        log.err(f"SPDX writer failed for modules-deps document: {e}; bailing")
        return False

    return True
