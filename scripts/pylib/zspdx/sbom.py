# Copyright (c) 2020, 2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import logging
import os
from dataclasses import dataclass

from packaging.version import Version

from .model import SBOMGraph
from .scanner import ScannerConfig, scanSBOMGraph
from .serializers.helpers import normalize_spdx_name
from .version import SPDX_VERSION_2_3
from .walker import Walker, WalkerConfig

_logger = logging.getLogger(__name__)


# Reference to a single sysbuild domain, used to build the system-level
# aggregate document that ties all domains together.
@dataclass
class _SystemDomainRef:
    name: str
    # SPDX 2.x: cross-document reference to the domain's build document
    build_namespace: str = ""
    build_sha1: str = ""
    final_image_id: str = ""
    # SPDX 3.0: IRI of the domain's final image package
    final_image_iri: str = ""


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
    spdxVersion: Version = SPDX_VERSION_2_3

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
            _logger.error(
                "cmake api query directory %s exists and is not a directory", cmakeApiDirPath
            )
            return False
        # directory exists, we're good
    else:
        # create the directory
        os.makedirs(cmakeApiDirPath, exist_ok=False)

    # check that codemodel-v2 exists as a file, or else create it
    queryFilePath = os.path.join(cmakeApiDirPath, "codemodel-v2")
    if os.path.exists(queryFilePath):
        if not os.path.isfile(queryFilePath):
            _logger.error("cmake api query file %s exists and is not a directory", queryFilePath)
            return False
        # file exists, we're good
        return True
    else:
        # file doesn't exist, let's create an empty file
        with open(queryFilePath, "w"):
            pass
        return True


def _make_serializer(cfg, sbom_graph):
    """Return a serializer instance for the configured SPDX version, or None."""
    if cfg.spdxVersion.major == 2:
        from zspdx.serializers.spdx2 import SPDX2Serializer

        return SPDX2Serializer(sbom_graph, cfg.spdxVersion)
    elif cfg.spdxVersion.major == 3:
        from zspdx.serializers.spdx3 import SPDX3Serializer

        return SPDX3Serializer(sbom_graph, cfg.spdxVersion)

    _logger.error("Unsupported SPDX version: %s", cfg.spdxVersion)
    return None


def _build_domain_sbom(cfg, build_dir, namespace_prefix, output_dir):
    """Walk, scan and serialize the SBOM for a single build directory.

    Returns the ``(Walker, serializer)`` pair on success, or ``None`` when the
    build directory could not be analyzed (e.g. it is not a Zephyr build) or
    serialization failed.
    """
    # set up walker configuration
    walkerCfg = WalkerConfig()
    walkerCfg.namespacePrefix = namespace_prefix
    walkerCfg.buildDir = build_dir
    walkerCfg.analyzeIncludes = cfg.analyzeIncludes
    walkerCfg.includeSDK = cfg.includeSDK

    # make and run the walker
    w = Walker(walkerCfg)
    sbom_graph = w.collectSBOMGraph()
    if not sbom_graph:
        return None

    # scan SBOM graph
    scanSBOMGraph(ScannerConfig(), sbom_graph)

    # route to appropriate serializer based on version
    serializer = _make_serializer(cfg, sbom_graph)
    if serializer is None:
        return None
    if not serializer.serialize(output_dir):
        return None

    return w, serializer


# main entry point for SBOM maker
# Arguments:
#   1) cfg: SBOMConfig
#   2) domains: optional list of (name, build_dir) tuples, one per sysbuild
#      domain. When omitted, a single (non-sysbuild) build is assumed.
#   3) sysbuild: whether cfg.buildDir is a sysbuild top-level build directory.
def makeSPDX(cfg, domains=None, sysbuild=False):
    # report any odd configuration settings
    if cfg.analyzeIncludes and not cfg.includeSDK:
        _logger.warning(
            "config: requested to analyze includes but not to generate SDK SPDX document;"
        )
        _logger.warning(
            "config: will proceed but will discard detected includes for SDK header files"
        )

    # Non-sysbuild build: generate a single set of documents directly into the
    # SPDX output directory, preserving the historical (flat) layout.
    if not sysbuild or not domains:
        if _build_domain_sbom(cfg, cfg.buildDir, cfg.namespacePrefix, cfg.spdxDir) is None:
            _logger.error("SPDX generation failed; bailing")
            return False
        return True

    # Sysbuild build: generate a separate set of documents for each domain in
    # its own subdirectory under the SPDX output directory.
    domain_refs = []
    for name, build_dir in domains:
        _logger.info("generating SPDX documents for domain '%s'", name)
        domain_dir = os.path.join(cfg.spdxDir, name)
        os.makedirs(domain_dir, exist_ok=True)
        namespace_prefix = f"{cfg.namespacePrefix}/{name}"
        result = _build_domain_sbom(cfg, build_dir, namespace_prefix, domain_dir)
        if result is None:
            _logger.warning(
                "skipping domain '%s': not a Zephyr build, or SPDX generation failed", name
            )
            continue
        domain_refs.append(_collect_domain_ref(cfg, name, *result))

    if not domain_refs:
        _logger.error("no SPDX documents could be generated for any domain; bailing")
        return False

    # Write the system-level aggregate tying all domains together.
    _write_system_document(cfg, domain_refs)

    return True


def _collect_domain_ref(cfg, name, walker, serializer):
    """Build the system-level reference describing a single generated domain."""
    ref = _SystemDomainRef(name=name)
    final_name = walker.final_image_name
    domain_namespace = walker.sbom_graph.namespace_prefix.rstrip("/")

    if cfg.spdxVersion.major == 2:
        build_doc = walker.sbom_graph.get_document("build")
        if build_doc:
            ref.build_namespace = build_doc.namespace
        ref.build_sha1 = serializer.document_hashes.get("build", "")
        if final_name:
            ref.final_image_id = serializer.component_ids.get(final_name, "")
    elif cfg.spdxVersion.major == 3 and final_name:
        ref.final_image_iri = f"{domain_namespace}/packages/{normalize_spdx_name(final_name)}"

    return ref


def _write_system_document(cfg, domain_refs):
    """Write the system-level aggregate document for all sysbuild domains."""
    system_namespace = f"{cfg.namespacePrefix}/sbom"
    # an empty graph is enough: the system document is built from domain_refs
    system_graph = SBOMGraph(namespace_prefix=system_namespace, build_dir=cfg.buildDir)
    serializer = _make_serializer(cfg, system_graph)
    if serializer is None:
        return False

    ext = "jsonld" if cfg.spdxVersion.major == 3 else "spdx"
    output_path = os.path.join(cfg.spdxDir, f"sbom.{ext}")
    return serializer.write_system_document(output_path, "system", domain_refs)
