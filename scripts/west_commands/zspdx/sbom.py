# Copyright (c) 2020, 2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import os
from dataclasses import dataclass

from west import log

from zspdx.scanner import ScannerConfig, scanSBOMData
from zspdx.version import SPDX_VERSION_2_2, SPDX_VERSION_2_3
from zspdx.walker import Walker, WalkerConfig


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

    # SPDX specification version to use (Version object from packaging.version)
    spdxVersion = SPDX_VERSION_2_3

    # should also analyze for included header files?
    analyzeIncludes: bool = False

    # should also add an SPDX document for the SDK?
    includeSDK: bool = False


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


# main entry point for SBOM maker
# Arguments:
#   1) cfg: SBOMConfig
def makeSPDX(cfg):
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

    # make and run the walker to collect SBOM data
    w = Walker(walkerCfg)
    sbom_data = w.collectSBOMData()
    if not sbom_data:
        log.err("SPDX walker failed; bailing")
        return False

    # set up scanner configuration
    scannerCfg = ScannerConfig()

    # scan SBOM data
    scanSBOMData(scannerCfg, sbom_data)

    # route to appropriate serializer based on version
    if cfg.spdxVersion.major == 2:
        # Use SPDX 2.x serializer
        from zspdx.serializers.spdx2 import SPDX2Serializer

        serializer = SPDX2Serializer(sbom_data, cfg.spdxVersion)
        return serializer.serialize(cfg.spdxDir)
    elif cfg.spdxVersion.major == 3:
        # Use SPDX 3.0 serializer (to be implemented)
        log.err("SPDX 3.0 serializer not yet implemented")
        return False
    else:
        log.err(f"Unsupported SPDX version: {cfg.spdxVersion}")
        return False
