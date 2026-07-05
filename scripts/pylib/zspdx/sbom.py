# Copyright (c) 2020, 2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import logging
import os
from dataclasses import dataclass

from packaging.version import Version

from zspdx.scanner import ScannerConfig, scan_sbom_graph
from zspdx.version import SPDX_VERSION_2_3, SPDX_VERSION_3_1
from zspdx.walker import Walker, WalkerConfig

_logger = logging.getLogger(__name__)


# SBOMConfig contains settings that will be passed along to the various
# SBOM maker subcomponents.
@dataclass(eq=True)
class SBOMConfig:
    # prefix for Document namespaces; should not end with "/"
    namespace_prefix: str = ""

    # location of build directory
    build_dir: str = ""

    # location of SPDX document output directory
    spdx_dir: str = ""

    # SPDX specification version to use
    spdx_version: Version = SPDX_VERSION_2_3

    # should also analyze for included header files?
    analyze_includes: bool = False

    # should also add an SPDX document for the SDK?
    include_sdk: bool = False

    # ---- SPDX 3.1 FunctionalSafety inputs (all optional) --------------------
    # Documentation traceability graph (doc/_build/html/traceability.json).
    traceability_json: str = ""

    # twister.json (or its output dir) providing test execution results.
    twister_json: str = ""

    # test_matrix.json providing per-test line coverage (true traceability).
    coverage_json: str = ""

    # reqmgmt StrictDoc module directory (full requirement statements).
    requirements_dir: str = ""


# create Cmake file-based API directories and query file
# Arguments:
#   1) build_dir: build directory
def setup_cmake_query(build_dir):
    # check that query dir exists as a directory, or else create it
    cmake_api_dir_path = os.path.join(build_dir, ".cmake", "api", "v1", "query")
    if os.path.exists(cmake_api_dir_path):
        if not os.path.isdir(cmake_api_dir_path):
            _logger.error(
                "cmake api query directory %s exists and is not a directory", cmake_api_dir_path
            )
            return False
        # directory exists, we're good
    else:
        # create the directory
        os.makedirs(cmake_api_dir_path, exist_ok=False)

    # request the codemodel (targets/sources) and toolchains-v1 (compiler ids and versions, used by
    # the SPDX 3.0 Build profile) file-based API objects by creating an empty query file for each
    for query_object in ("codemodel-v2", "toolchains-v1"):
        query_file_path = os.path.join(cmake_api_dir_path, query_object)
        if os.path.exists(query_file_path):
            if not os.path.isfile(query_file_path):
                _logger.error("cmake api query file %s exists and is not a file", query_file_path)
                return False
            # file exists, we're good
            continue
        # file doesn't exist, let's create an empty file
        with open(query_file_path, "w"):
            pass

    return True


def _zephyr_base(build_dir: str) -> str:
    """Best-effort ZEPHYR_BASE: the environment, else the build's CMake cache."""
    from zspdx.requirements import _read_cache_vars

    base = os.environ.get("ZEPHYR_BASE", "")
    if not base:
        base = _read_cache_vars(build_dir, {"ZEPHYR_BASE"}).get("ZEPHYR_BASE", "")
    return base


def _load_functional_safety_inputs(cfg, sbom_graph):
    """Load the traceability / results / coverage / catalog inputs.

    Everything is keyed off the traceability graph: without it there is no
    FunctionalSafety data to emit, so this is a no-op (and the serializer falls
    back to a plain SBOM). The loaded objects are stashed on
    ``sbom_graph.metadata`` for the SPDX 3.1 serializer to consume.

    Coverage answers the "true traceability" question — does a requirement's
    verifying tests actually exercise its implementing code? So each requirement's
    implementing symbols are resolved to their function bodies (against the
    sources of the coverage build's commit), and the per-test line coverage is
    indexed for the serializer to intersect against.
    """
    from zspdx.coverage import load_matrix
    from zspdx.requirements import load_requirements_catalog
    from zspdx.sources import Source, resolve_impl_symbols
    from zspdx.traceability import load_traceability
    from zspdx.twister import load_results

    traceability_path = cfg.traceability_json
    if not traceability_path:
        return
    traceability = load_traceability(traceability_path)
    if not traceability:
        return
    sbom_graph.metadata["traceability"] = traceability

    zephyr_base = _zephyr_base(cfg.build_dir)
    sbom_graph.metadata["zephyr_base"] = zephyr_base

    sbom_graph.metadata["requirements_catalog"] = load_requirements_catalog(
        build_dir=cfg.build_dir, explicit=cfg.requirements_dir
    )

    env, results, qual_map = ({}, {}, {})
    if cfg.twister_json:
        env, results, qual_map = load_results(cfg.twister_json)
        sbom_graph.metadata["test_results"] = results

    # Resolve implementing symbols to their bodies against the coverage build's
    # sources, and index the coverage of the graph's tests to intersect them.
    if zephyr_base:
        source = Source(zephyr_base, env.get("zephyr_version", ""))
        sbom_graph.metadata["impl_bodies"] = resolve_impl_symbols(
            source, traceability.implementation_symbols()
        )
        sbom_graph.metadata["source"] = source

    if cfg.coverage_json and qual_map:
        sbom_graph.metadata["coverage"] = load_matrix(
            cfg.coverage_json, qual_map, wanted_tests=set(traceability.tests)
        )


# main entry point for SBOM maker
# Arguments:
#   1) cfg: SBOMConfig
def make_spdx(cfg):
    # report any odd configuration settings
    if cfg.analyze_includes and not cfg.include_sdk:
        _logger.warning(
            "config: requested to analyze includes but not to generate SDK SPDX document;"
        )
        _logger.warning(
            "config: will proceed but will discard detected includes for SDK header files"
        )

    # set up walker configuration
    walker_cfg = WalkerConfig()
    walker_cfg.namespace_prefix = cfg.namespace_prefix
    walker_cfg.build_dir = cfg.build_dir
    walker_cfg.analyze_includes = cfg.analyze_includes
    walker_cfg.include_sdk = cfg.include_sdk

    # make and run the walker
    w = Walker(walker_cfg)
    sbom_graph = w.collect_sbom_graph()
    if not sbom_graph:
        _logger.error("SPDX walker failed; bailing")
        return False

    # set up scanner configuration
    scanner_cfg = ScannerConfig()

    # scan SBOM graph
    scan_sbom_graph(scanner_cfg, sbom_graph)

    # load the SPDX 3.1 FunctionalSafety inputs, if requested
    if cfg.spdx_version >= SPDX_VERSION_3_1:
        _load_functional_safety_inputs(cfg, sbom_graph)

    # route to appropriate serializer based on version
    if cfg.spdx_version.major == 2:
        # Use SPDX 2.x serializer
        from zspdx.serializers.spdx2 import SPDX2Serializer

        serializer = SPDX2Serializer(sbom_graph, cfg.spdx_version)
        return serializer.serialize(cfg.spdx_dir)
    elif cfg.spdx_version.major == 3:
        # Use SPDX 3.0 serializer
        from zspdx.serializers.spdx3 import SPDX3Serializer

        serializer = SPDX3Serializer(sbom_graph, cfg.spdx_version)
        return serializer.serialize(cfg.spdx_dir)
    else:
        _logger.error("Unsupported SPDX version: %s", cfg.spdx_version)
        return False
