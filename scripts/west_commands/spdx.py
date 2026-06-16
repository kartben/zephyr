# Copyright (c) 2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import logging
import os
import sys
import uuid

from west.commands import WestCommand

from build_helpers import forward_logging_to_west, load_domains

script_dir = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
sys.path.insert(0, os.path.join(script_dir, "pylib/"))
from zspdx.sbom import SBOMConfig, makeSPDX, setupCmakeQuery  # noqa: E402
from zspdx.version import SPDX_VERSION_2_3, SUPPORTED_SPDX_VERSIONS, parse  # noqa: E402

SPDX_DESCRIPTION = """\
This command creates an SPDX bill of materials following the completion
of a Zephyr build.

Prior to the build, the CMake file-based API must be enabled, as the SPDX
command relies upon it. This is done by calling `west spdx --init` before
calling `west build`. When the build directory is a sysbuild build, `--init`
also enables the file-based API for every domain so that an SBOM can be
generated for each of them."""


class ZephyrSpdx(WestCommand):
    def __init__(self):
        super().__init__('spdx', '', description=SPDX_DESCRIPTION)

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(self.name, description=self.description)

        # If you update these options, make sure to keep the docs in
        # doc/guides/west/zephyr-cmds.rst up to date.
        parser.add_argument(
            '-i',
            '--init',
            action="store_true",
            help="initialize CMake file-based API prior to building; for a sysbuild "
            "build directory this also initializes every domain",
        )
        parser.add_argument('-d', '--build-dir', help="build directory")
        parser.add_argument('-n', '--namespace-prefix', help="namespace prefix")
        parser.add_argument('-s', '--spdx-dir', help="SPDX output directory")
        parser.add_argument(
            '--spdx-version',
            choices=[str(v) for v in SUPPORTED_SPDX_VERSIONS],
            default=str(SPDX_VERSION_2_3),
            help="SPDX specification version to use (default: 2.3)",
        )
        parser.add_argument(
            '--analyze-includes', action="store_true", help="also analyze included header files"
        )
        parser.add_argument(
            '--include-sdk', action="store_true", help="also generate SPDX document for SDK"
        )

        return parser

    def do_run(self, args, unknown_args):
        # Forward debug output from the zspdx package so module-level
        # logging is visible under "west -v" / "west -vv".
        forward_logging_to_west(self, 'zspdx')
        logging.getLogger('zspdx').propagate = False

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

        # Initialize the CMake file-based API for the top-level build directory.
        # When run before the build, this is all that exists yet; the sysbuild
        # build system propagates this query to each domain at configure time.
        # When run after a sysbuild build (domains.yaml present), also initialize
        # each existing domain build directory directly.
        build_dirs = [args.build_dir]
        if os.path.isfile(os.path.join(args.build_dir, "domains.yaml")):
            domains = [d.build_dir for d in load_domains(args.build_dir).get_domains()]
            build_dirs.extend(d for d in domains if d not in build_dirs)
            self.inf(f"sysbuild detected; also initializing domains: {', '.join(domains)}")

        for build_dir in build_dirs:
            if not setupCmakeQuery(build_dir):
                self.die(
                    f"Couldn't create CMake file-based API query directory in {build_dir}\n"
                    "You can manually create an empty file at "
                    "$BUILDDIR/.cmake/api/v1/query/codemodel-v2"
                )

        self.inf("initialized; run `west build` then run `west spdx`")

    def do_run_spdx(self, args):
        if not args.build_dir:
            self.die("Build directory not specified; call `west spdx --build-dir=BUILD_DIR`")

        # create the SPDX files
        cfg = SBOMConfig()
        cfg.buildDir = args.build_dir
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
            cfg.spdxDir = os.path.join(args.build_dir, "spdx")
        if args.analyze_includes:
            cfg.analyzeIncludes = True
        if args.include_sdk:
            cfg.includeSDK = True

        # When the build directory is a sysbuild top-level directory it
        # contains a domains.yaml listing the build directory of each domain
        # (the application, MCUboot, etc.). In that case generate a separate
        # set of SPDX documents for each domain.
        sysbuild = os.path.isfile(os.path.join(args.build_dir, "domains.yaml"))
        domains = None
        if sysbuild:
            domains = [(d.name, d.build_dir) for d in load_domains(args.build_dir).get_domains()]
            self.inf(f"sysbuild detected; generating SPDX for domains: "
                     f"{', '.join(name for name, _ in domains)}")

        # make sure SPDX directory exists, or create it if it doesn't
        if os.path.exists(cfg.spdxDir):
            if not os.path.isdir(cfg.spdxDir):
                self.err(f'SPDX output directory {cfg.spdxDir} exists but is not a directory')
                return
            # directory exists, we're good
        else:
            # create the directory
            os.makedirs(cfg.spdxDir, exist_ok=False)

        if not makeSPDX(cfg, domains=domains, sysbuild=sysbuild):
            self.die("Failed to create SPDX output")
