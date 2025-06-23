# Copyright (c) 2020, 2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import os
from dataclasses import dataclass

from west import log

from zspdx.scanner import ScannerConfig, scanDocument
from zspdx.version import SPDX_VERSION_2_3, SPDX_VERSION_3_0
from zspdx.walker import Walker, WalkerConfig
from zspdx.writer import writeSPDX


# SBOMConfig contains settings that will be passed along to the various
# SBOM maker subcomponents.
@dataclass(eq=True)
class SBOMConfig:
    # prefix for Document namespaces; should not end with "/"
    namespacePrefix: str = ""

    # location of build directory
    buildDir: str = ""

        # location of SPDX document output directory
    spdxDir: str = ""

    # SPDX specification version to use
    spdxVersion: str = SPDX_VERSION_2_3

    # should also analyze for included header files?
    analyzeIncludes: bool = False

    # should also add an SPDX document for the SDK?
    includeSDK: bool = False

    # use native SPDX3 approach (experimental)
    useNativeSPDX3: bool = False


# create Cmake file-based API directories and query file
# Arguments:
#   1) build_dir: build directory
def setupCmakeQuery(build_dir):
    # check that query dir exists as a directory, or else create it
    cmakeApiDirPath = os.path.join(build_dir, ".cmake", "api", "v1", "query")
    if os.path.exists(cmakeApiDirPath):
        if not os.path.isdir(cmakeApiDirPath):
            log.err(f'cmake api query directory {cmakeApiDirPath} exists and is not a directory')
            return False
        # directory exists, we're good
    else:
        # create the directory
        os.makedirs(cmakeApiDirPath, exist_ok=False)

    # check that codemodel-v2 exists as a file, or else create it
    queryFilePath = os.path.join(cmakeApiDirPath, "codemodel-v2")
    if os.path.exists(queryFilePath):
        if not os.path.isfile(queryFilePath):
            log.err(f'cmake api query file {queryFilePath} exists and is not a directory')
            return False
        # file exists, we're good
        return True
    else:
        # file doesn't exist, let's create an empty file
        with open(queryFilePath, "w"):
            pass
        return True


def makeSPDXNative(cfg):
    """Generate SPDX using the native SPDX3 approach (experimental)"""
    if cfg.spdxVersion < SPDX_VERSION_3_0:
        log.err("Native SPDX3 approach only supports SPDX 3.0+")
        return False

    try:
        # Import here to avoid circular imports
        from zspdx.spdx3_native_walker import SPDX3NativeWalker

        # Create native walker configuration
        native_config = type(
            'NativeConfig',
            (),
            {
                'namespacePrefix': cfg.namespacePrefix,
                'buildDir': cfg.buildDir,
                'spdxDir': cfg.spdxDir,
                'analyzeIncludes': cfg.analyzeIncludes,
                'includeSDK': cfg.includeSDK,
            },
        )()

        # Create and run the native walker
        walker = SPDX3NativeWalker(native_config)
        return walker.generate_sbom()

    except ImportError as e:
        log.err(f"Native SPDX3 walker not available: {e}")
        log.inf("Falling back to legacy approach")
        return makeSPDX(cfg)
    except Exception as e:
        log.err(f"Native SPDX3 generation failed: {e}")
        log.inf("Falling back to legacy approach")
        return makeSPDX(cfg)


# main entry point for SBOM maker
# Arguments:
#   1) cfg: SBOMConfig
def makeSPDX(cfg):
    # Use native approach if requested and supported
    if cfg.useNativeSPDX3 and cfg.spdxVersion >= SPDX_VERSION_3_0:
        log.inf("Using native SPDX3 approach")
        return makeSPDXNative(cfg)

    # report any odd configuration settings
    if cfg.analyzeIncludes and not cfg.includeSDK:
        log.wrn("config: requested to analyze includes but not to generate SDK SPDX document;")
        log.wrn("config: will proceed but will discard detected includes for SDK header files")

    # set up walker configuration
    walkerCfg = WalkerConfig()
    walkerCfg.namespacePrefix = cfg.namespacePrefix
    walkerCfg.buildDir = cfg.buildDir
    walkerCfg.analyzeIncludes = cfg.analyzeIncludes
    walkerCfg.includeSDK = cfg.includeSDK

    # make and run the walker
    w = Walker(walkerCfg)
    retval = w.makeDocuments()
    if not retval:
        log.err("SPDX walker failed; bailing")
        return False

    # set up scanner configuration
    scannerCfg = ScannerConfig()

    # scan each document from walker
    if cfg.includeSDK:
        scanDocument(scannerCfg, w.docSDK)
    scanDocument(scannerCfg, w.docApp)
    scanDocument(scannerCfg, w.docZephyr)
    scanDocument(scannerCfg, w.docBuild)

    if cfg.spdxVersion < SPDX_VERSION_3_0:
        # write SDK document, if we made one
        if cfg.includeSDK:
            retval = writeSPDX(os.path.join(cfg.spdxDir, "sdk.spdx"), w.docSDK, cfg.spdxVersion)
            if not retval:
                log.err("SPDX writer failed for SDK document; bailing")
                return False

        # write app document
        retval = writeSPDX(os.path.join(cfg.spdxDir, "app.spdx"), w.docApp, cfg.spdxVersion)
        if not retval:
            log.err("SPDX writer failed for app document; bailing")
            return False

        # write zephyr document
        retval = writeSPDX(os.path.join(cfg.spdxDir, "zephyr.spdx"), w.docZephyr, cfg.spdxVersion)
        if not retval:
            log.err("SPDX writer failed for zephyr document; bailing")
            return False

        # write build document
        retval = writeSPDX(os.path.join(cfg.spdxDir, "build.spdx"), w.docBuild, cfg.spdxVersion)
        if not retval:
            log.err("SPDX writer failed for build document; bailing")
            return False

        # write modules document
        retval = writeSPDX(
            os.path.join(cfg.spdxDir, "modules-deps.spdx"), w.docModulesExtRefs, cfg.spdxVersion
        )
        if not retval:
            log.err("SPDX writer failed for modules-deps document; bailing")
            return False

        return True
    else:  # SPDX 3.0
        # Import here to avoid circular imports
        from zspdx.spdx3_generator import SPDX3Config, SPDX3Generator

        # Convert SBOMConfig to SPDX3Config
        spdx3_cfg = SPDX3Config()
        spdx3_cfg.namespacePrefix = cfg.namespacePrefix
        spdx3_cfg.buildDir = cfg.buildDir
        spdx3_cfg.spdxDir = cfg.spdxDir
        spdx3_cfg.analyzeIncludes = cfg.analyzeIncludes
        spdx3_cfg.includeSDK = cfg.includeSDK

        # Generate SPDX 3.0 using the walker results
        generator = SPDX3Generator(spdx3_cfg)
        return generator.generate_spdx3_from_walker(w)
