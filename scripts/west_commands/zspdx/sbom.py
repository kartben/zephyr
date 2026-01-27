# Copyright (c) 2020, 2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import os
from dataclasses import dataclass, field
from enum import Enum
from typing import Optional

from west import log

from zspdx.scanner import ScannerConfig, scanSBOMData
from zspdx.version import SPDX_VERSION_2_2, SPDX_VERSION_2_3
from zspdx.walker import Walker, WalkerConfig


class SBOMFormat(Enum):
    """Supported SBOM output formats."""
    SPDX = "spdx"           # SPDX format (version determined by spdxVersion)
    CYCLONEDX = "cyclonedx" # CycloneDX format


# SBOMConfig contains settings that will be passed along to the various
# SBOM maker subcomponents.
@dataclass(eq=True)
class SBOMConfig:
    # prefix for Document namespaces; should not end with "/"
    namespacePrefix: str = ""

    # location of build directory
    buildDir: str = ""

    # location of SBOM document output directory
    spdxDir: str = ""

    # Output format (SPDX or CycloneDX)
    outputFormat: SBOMFormat = SBOMFormat.SPDX

    # SPDX specification version to use (Version object from packaging.version)
    # Only used when outputFormat is SPDX
    spdxVersion = SPDX_VERSION_2_3

    # CycloneDX schema version (e.g., "1.5", "1.6")
    # Only used when outputFormat is CYCLONEDX
    cyclonedxVersion: str = "1.5"

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
def makeSBOM(cfg):
    """
    Create SBOM documents in the specified format.

    Args:
        cfg: SBOMConfig with build directory, output settings, and format selection.

    Returns:
        True if SBOM generation succeeded, False otherwise.
    """
    # report any odd configuration settings
    if cfg.analyzeIncludes and not cfg.includeSDK:
        log.wrn("config: requested to analyze includes but not to generate SDK SBOM document;")
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
        log.err("SBOM walker failed; bailing")
        return False

    # set up scanner configuration
    scannerCfg = ScannerConfig()

    # scan SBOM data
    scanSBOMData(scannerCfg, sbom_data)

    # route to appropriate serializer based on format
    if cfg.outputFormat == SBOMFormat.CYCLONEDX:
        return _serialize_cyclonedx(sbom_data, cfg)
    else:
        return _serialize_spdx(sbom_data, cfg)


def _serialize_spdx(sbom_data, cfg):
    """Serialize SBOM data to SPDX format."""
    if cfg.spdxVersion.major == 2:
        # Use SPDX 2.x serializer
        from zspdx.serializers.spdx2 import SPDX2Serializer

        serializer = SPDX2Serializer(sbom_data, cfg.spdxVersion)
        return serializer.serialize(cfg.spdxDir)
    elif cfg.spdxVersion.major == 3:
        # Use SPDX 3.0 serializer
        from zspdx.serializers.spdx3 import SPDX3Serializer

        serializer = SPDX3Serializer(sbom_data, cfg.spdxVersion)
        return serializer.serialize(cfg.spdxDir)
    else:
        log.err(f"Unsupported SPDX version: {cfg.spdxVersion}")
        return False


def _serialize_cyclonedx(sbom_data, cfg):
    """Serialize SBOM data to CycloneDX format."""
    try:
        from cyclonedx.schema import SchemaVersion
    except ImportError:
        log.err("CycloneDX library not installed. Install with: pip install cyclonedx-python-lib")
        return False

    from zspdx.serializers.cyclonedx import CycloneDXSerializer

    # Map version string to SchemaVersion
    version_map = {
        "1.4": SchemaVersion.V1_4,
        "1.5": SchemaVersion.V1_5,
        "1.6": SchemaVersion.V1_6,
    }

    schema_version = version_map.get(cfg.cyclonedxVersion)
    if not schema_version:
        log.err(f"Unsupported CycloneDX version: {cfg.cyclonedxVersion}. Supported: 1.4, 1.5, 1.6")
        return False

    serializer = CycloneDXSerializer(sbom_data, schema_version)
    return serializer.serialize(cfg.spdxDir)


# Backward compatibility alias
def makeSPDX(cfg):
    """
    Create SBOM documents (backward compatibility alias for makeSBOM).

    This function is kept for backward compatibility. New code should use makeSBOM().
    """
    return makeSBOM(cfg)
