# Copyright (c) 2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import os
import uuid

from west.commands import WestCommand

from build_helpers import zephyr_cmake_build_dir_for_spdx
from zspdx.sbom import SBOMConfig, makeSPDX, setupCmakeQuery
from zspdx.version import SPDX_VERSION_2_3, SUPPORTED_SPDX_VERSIONS, parse

SPDX_DESCRIPTION = """\
This command creates an SPDX 2.2 or 2.3 tag-value bill of materials
following the completion of a Zephyr build.

Prior to the build, an empty file must be created at
BUILDDIR/.cmake/api/v1/query/codemodel-v2 in order to enable
the CMake file-based API, which the SPDX command relies upon.
This can be done by calling `west spdx --init` prior to
calling `west build`.

For sysbuild, pass the top-level sysbuild directory (the same path you use
with ``west build --sysbuild -d``). The command uses ``domains.yaml`` to
find the default image's Zephyr build directory and initializes the CMake
API there."""

class ZephyrSpdx(WestCommand):
    def __init__(self):
        super().__init__(
                'spdx',
                '',
                description=SPDX_DESCRIPTION)

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(self.name,
                description = self.description)

        # If you update these options, make sure to keep the docs in
        # doc/guides/west/zephyr-cmds.rst up to date.
        parser.add_argument('-i', '--init', action="store_true",
                help="initialize CMake file-based API")
        parser.add_argument('-d', '--build-dir',
                help="build directory")
        parser.add_argument('-n', '--namespace-prefix',
                help="namespace prefix")
        parser.add_argument('-s', '--spdx-dir',
                help="SPDX output directory")
        parser.add_argument('--spdx-version', choices=[str(v) for v in SUPPORTED_SPDX_VERSIONS],
                default=str(SPDX_VERSION_2_3),
                help="SPDX specification version to use (default: 2.3)")
        parser.add_argument('--analyze-includes', action="store_true",
                help="also analyze included header files")
        parser.add_argument('--include-sdk', action="store_true",
                help="also generate SPDX document for SDK")

        return parser

    def do_run(self, args, unknown_args):
        self.dbg("running zephyr SPDX generator")

        self.dbg("  --init is", args.init)
        self.dbg("  --build-dir is", args.build_dir)
        self.dbg("  --namespace-prefix is", args.namespace_prefix)
        self.dbg("  --spdx-dir is", args.spdx_dir)
        self.dbg("  --spdx-version is", args.spdx_version)
        self.dbg("  --analyze-includes is", args.analyze_includes)
        self.dbg("  --include-sdk is", args.include_sdk)

        if args.init:
            self.do_run_init(args)
        else:
            self.do_run_spdx(args)

    def do_run_init(self, args):
        self.inf("initializing CMake file-based API prior to build")

        if not args.build_dir:
            self.die("Build directory not specified; call `west spdx --init --build-dir=BUILD_DIR`")

        image_dir, sysbuild_top = zephyr_cmake_build_dir_for_spdx(args.build_dir)
        # initialize CMake file-based API - empty query file
        query_ready = setupCmakeQuery(image_dir)
        if query_ready:
            if sysbuild_top:
                self.inf(
                    f"sysbuild: initialized CMake API under default image {image_dir}; "
                    "run `west build --sysbuild` then `west spdx -d` with the same top-level build dir"
                )
            else:
                self.inf("initialized; run `west build` then run `west spdx`")
        else:
            self.die("Couldn't create CMake file-based API query directory\n"
                     "You can manually create an empty file at "
                     "$BUILDDIR/.cmake/api/v1/query/codemodel-v2")

    def do_run_spdx(self, args):
        if not args.build_dir:
            self.die("Build directory not specified; call `west spdx --build-dir=BUILD_DIR`")

        image_dir, _sysbuild_top = zephyr_cmake_build_dir_for_spdx(args.build_dir)

        # create the SPDX files
        cfg = SBOMConfig()
        cfg.buildDir = image_dir
        try:
            version_obj = parse(args.spdx_version)
        except Exception:
            self.die(f"Invalid SPDX version: {args.spdx_version}")
        cfg.spdxVersion = version_obj
        if args.namespace_prefix:
            cfg.namespacePrefix = args.namespace_prefix
        else:
            # create default namespace according to SPDX spec
            # note that this is intentionally _not_ an actual URL where
            # this document will be stored
            cfg.namespacePrefix = f"http://spdx.org/spdxdocs/zephyr-{str(uuid.uuid4())}"
        if args.spdx_dir:
            cfg.spdxDir = args.spdx_dir
        else:
            cfg.spdxDir = os.path.join(image_dir, "spdx")
        if args.analyze_includes:
            cfg.analyzeIncludes = True
        if args.include_sdk:
            cfg.includeSDK = True

        # make sure SPDX directory exists, or create it if it doesn't
        if os.path.exists(cfg.spdxDir):
            if not os.path.isdir(cfg.spdxDir):
                self.err(f'SPDX output directory {cfg.spdxDir} exists but is not a directory')
                return
            # directory exists, we're good
        else:
            # create the directory
            os.makedirs(cfg.spdxDir, exist_ok=False)

        if not makeSPDX(cfg):
            self.die("Failed to create SPDX output")
