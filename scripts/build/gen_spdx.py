#!/usr/bin/env python3
#
# Copyright (c) 2026 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""Generate SPDX documents for a Zephyr build directory.

This is the build system entry point behind the ``spdx`` build target
(``west build -t spdx``). It drives the same machinery as the ``west spdx``
command, with the same defaults, but does not require west to be installed.
"""

import argparse
import logging
import os
import sys
import uuid

sys.path.insert(
    0, os.path.join(os.path.dirname(os.path.dirname(os.path.realpath(__file__))), "pylib")
)
from zspdx.sbom import SBOMConfig, ensure_cmake_file_api, make_spdx  # noqa: E402
from zspdx.version import SPDX_VERSION_2_3, SUPPORTED_SPDX_VERSIONS, parse  # noqa: E402


def parse_args():
    parser = argparse.ArgumentParser(allow_abbrev=False, description=__doc__)
    parser.add_argument("--build-dir", required=True, help="build directory")
    parser.add_argument("--spdx-dir", help="SPDX output directory (default: BUILD_DIR/spdx)")
    parser.add_argument("--namespace-prefix", help="namespace prefix")
    parser.add_argument(
        "--spdx-version",
        choices=[str(v) for v in SUPPORTED_SPDX_VERSIONS],
        default=str(SPDX_VERSION_2_3),
        help="SPDX specification version to use (default: 2.3)",
    )
    parser.add_argument(
        "--analyze-includes", action="store_true", help="also analyze included header files"
    )
    parser.add_argument(
        "--include-sdk", action="store_true", help="also generate SPDX document for SDK"
    )
    parser.add_argument(
        "--init-file-api",
        action="store_true",
        help="only make sure the CMake file-based API replies are available, "
        "re-running CMake if needed, then exit",
    )
    return parser.parse_args()


def main():
    args = parse_args()

    logging.basicConfig(level=logging.INFO, format="%(message)s")

    if args.init_file_api:
        if not ensure_cmake_file_api(args.build_dir):
            sys.exit("Failed to enable the CMake file-based API")
        return

    cfg = SBOMConfig()
    cfg.build_dir = args.build_dir
    cfg.spdx_version = parse(args.spdx_version)
    if args.namespace_prefix:
        cfg.namespace_prefix = args.namespace_prefix
    else:
        # create default namespace according to SPDX spec
        # note that this is intentionally _not_ an actual URL where
        # this document will be stored
        cfg.namespace_prefix = f"http://spdx.org/spdxdocs/zephyr-{uuid.uuid4()}"
    cfg.spdx_dir = args.spdx_dir or os.path.join(args.build_dir, "spdx")
    cfg.analyze_includes = args.analyze_includes
    cfg.include_sdk = args.include_sdk

    os.makedirs(cfg.spdx_dir, exist_ok=True)

    if not make_spdx(cfg):
        sys.exit("Failed to create SPDX output")


if __name__ == "__main__":
    main()
