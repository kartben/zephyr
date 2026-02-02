# Copyright (c) 2020, 2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import os
from dataclasses import dataclass

from west import log

from zspdx.scanner import ScannerConfig, scanDocument
from zspdx.spdx_adapter import SPDXAdapter
from zspdx.version import SPDX_VERSION_2_3
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
    walkerCfg.buildDir = cfg.buildDir
    walkerCfg.analyzeIncludes = cfg.analyzeIncludes
    walkerCfg.includeSDK = cfg.includeSDK

    # make and run the walker - produces generic SBOM structures
    w = Walker(walkerCfg)
    retval = w.makeDocuments()
    if not retval:
        log.err("SBOM walker failed; bailing")
        return False

    # create SPDX adapter to convert generic structures to SPDX format
    adapter = SPDXAdapter(namespacePrefix=cfg.namespacePrefix)

    # collect generic documents for conversion
    genericDocs = [w.docBuild, w.docZephyr, w.docApp]
    if cfg.includeSDK:
        genericDocs.insert(0, w.docSDK)
    if w.docModulesExtRefs:
        genericDocs.append(w.docModulesExtRefs)

    # convert all generic documents to SPDX format
    spdxDocs = adapter.convert_all_documents(genericDocs)

    # map converted documents back to named references
    docIndex = 0
    docSDK = None
    docModulesExtRefs = None
    
    if cfg.includeSDK:
        docSDK = spdxDocs[docIndex]
        docIndex += 1
    docBuild = spdxDocs[docIndex]
    docIndex += 1
    docZephyr = spdxDocs[docIndex]
    docIndex += 1
    docApp = spdxDocs[docIndex]
    docIndex += 1
    if w.docModulesExtRefs:
        docModulesExtRefs = spdxDocs[docIndex]

    # set up scanner configuration
    scannerCfg = ScannerConfig()

    # scan each SPDX document
    if docSDK:
        scanDocument(scannerCfg, docSDK)
    scanDocument(scannerCfg, docApp)
    scanDocument(scannerCfg, docZephyr)
    scanDocument(scannerCfg, docBuild)

    # write each document, in this particular order so that the
    # hashes for external references are calculated

    # write SDK document, if we made one
    if docSDK:
        retval = writeSPDX(os.path.join(cfg.spdxDir, "sdk.spdx"), docSDK, cfg.spdxVersion)
        if not retval:
            log.err("SPDX writer failed for SDK document; bailing")
            return False

    # write app document
    retval = writeSPDX(os.path.join(cfg.spdxDir, "app.spdx"), docApp, cfg.spdxVersion)
    if not retval:
        log.err("SPDX writer failed for app document; bailing")
        return False

    # write zephyr document
    retval = writeSPDX(os.path.join(cfg.spdxDir, "zephyr.spdx"), docZephyr, cfg.spdxVersion)
    if not retval:
        log.err("SPDX writer failed for zephyr document; bailing")
        return False

    # write build document
    retval = writeSPDX(os.path.join(cfg.spdxDir, "build.spdx"), docBuild, cfg.spdxVersion)
    if not retval:
        log.err("SPDX writer failed for build document; bailing")
        return False

    # write modules document, if we have one
    if docModulesExtRefs:
        retval = writeSPDX(
            os.path.join(cfg.spdxDir, "modules-deps.spdx"), docModulesExtRefs, cfg.spdxVersion
        )
        if not retval:
            log.err("SPDX writer failed for modules-deps document; bailing")
            return False

    return True
