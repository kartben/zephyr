# Copyright (c) 2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import contextlib
import logging
import os
import subprocess
import sys
import tempfile
import uuid

from west.commands import WestCommand

from build_helpers import forward_logging_to_west

script_dir = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
sys.path.insert(0, os.path.join(script_dir, "pylib/"))
from zspdx.sbom import SBOMConfig, make_spdx, setup_cmake_query  # noqa: E402
from zspdx.version import (  # noqa: E402
    SPDX_VERSION_2_3,
    SPDX_VERSION_3_1,
    SUPPORTED_SPDX_VERSIONS,
    parse,
)

SPDX_DESCRIPTION = """\
This command creates an SPDX bill of materials following the completion
of a Zephyr build.

Enable CONFIG_BUILD_OUTPUT_META in the application and build it as usual.
The build then asks CMake for the file-based API this command reads, so the
build directory needs no preparation.

Pass --modules-only to instead describe just the dependencies the west
manifest pulls in. That document is derived from module metadata rather than
from build output, so it needs no build directory and no build."""


class ZephyrSpdx(WestCommand):
    def __init__(self):
        super().__init__('spdx', '', description=SPDX_DESCRIPTION)

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(self.name, description=self.description)

        # If you update these options, make sure to keep the docs in
        # doc/develop/west/zephyr-cmds.rst up to date.
        parser.add_argument(
            '-i',
            '--init',
            action="store_true",
            help="[DEPRECATED] initialize CMake file-based API; a build with "
            "CONFIG_BUILD_OUTPUT_META now requests it itself",
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
        parser.add_argument(
            '--modules-only',
            action="store_true",
            help="describe only the dependencies pulled in by the west manifest, "
            "without requiring a build",
        )
        parser.add_argument(
            '--meta',
            help="module meta file to read with --modules-only; when omitted, one is "
            "generated from the current west workspace",
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
        self.dbg("  --modules-only is", args.modules_only)
        self.dbg("  --meta is", args.meta)

        if args.init:
            self.do_run_init(args)
        else:
            self.do_run_spdx(args)

    def do_run_init(self, args):
        self.wrn(
            "west spdx --init is deprecated and will be removed in Zephyr 5.0: "
            "a build with CONFIG_BUILD_OUTPUT_META requests the CMake file-based API "
            "itself, so the build directory no longer needs to be prepared."
        )
        self.inf("initializing CMake file-based API prior to build")

        if not args.build_dir:
            self.die("Build directory not specified; call `west spdx --init --build-dir=BUILD_DIR`")

        # initialize CMake file-based API - empty query file
        query_ready = setup_cmake_query(args.build_dir)
        if query_ready:
            self.inf("initialized; run `west build` then run `west spdx`")
        else:
            self.die(
                "Couldn't create CMake file-based API query directory\n"
                "You can manually create an empty file at "
                "$BUILDDIR/.cmake/api/v1/query/codemodel-v2"
            )

    def generate_module_meta(self, meta_path):
        """Write a module meta file for the current west workspace.

        This is the same generator a build runs as a post-build step, invoked
        directly so that --modules-only does not need one.
        """
        zephyr_base = os.path.dirname(script_dir)
        cmd = [
            sys.executable,
            os.path.join(script_dir, "zephyr_module.py"),
            f"--zephyr-base={zephyr_base}",
            "--meta-out",
            meta_path,
        ]
        self.dbg("generating module meta file:", " ".join(cmd))
        try:
            subprocess.run(cmd, check=True)
        except subprocess.CalledProcessError as e:
            self.die(
                f"Failed to collect module metadata (exit {e.returncode}).\n"
                "`west spdx --modules-only` must run inside a west workspace; "
                "pass --meta=FILE to use an already generated meta file instead."
            )

    def do_run_spdx(self, args):
        if not args.modules_only and not args.build_dir:
            self.die("Build directory not specified; call `west spdx --build-dir=BUILD_DIR`")

        if args.meta and not args.modules_only:
            self.wrn("--meta only applies to --modules-only; ignoring it")

        # create the SPDX files
        cfg = SBOMConfig()
        cfg.build_dir = args.build_dir or ""
        cfg.modules_only = args.modules_only
        try:
            version_obj = parse(args.spdx_version)
        except Exception:
            self.die(f"Invalid SPDX version: {args.spdx_version}")
        cfg.spdx_version = version_obj
        if version_obj == SPDX_VERSION_3_1:
            self.wrn("SPDX 3.1 support is experimental; the 3.1 spec is still in development.")
        if args.namespace_prefix:
            cfg.namespace_prefix = args.namespace_prefix
        else:
            # create default namespace according to SPDX spec
            # note that this is intentionally _not_ an actual URL where
            # this document will be stored
            cfg.namespace_prefix = f"http://spdx.org/spdxdocs/zephyr-{str(uuid.uuid4())}"
        if args.spdx_dir:
            cfg.spdx_dir = args.spdx_dir
        elif args.modules_only:
            cfg.spdx_dir = os.path.join(os.getcwd(), "spdx")
        else:
            cfg.spdx_dir = os.path.join(args.build_dir, "spdx")
        if args.analyze_includes:
            cfg.analyze_includes = True
        if args.include_sdk:
            cfg.include_sdk = True

        # Both of these describe what a build compiled, so neither has anything
        # to report when no build was walked.
        if args.modules_only:
            for enabled, name in (
                (args.analyze_includes, "--analyze-includes"),
                (args.include_sdk, "--include-sdk"),
            ):
                if enabled:
                    self.wrn(f"{name} has no effect with --modules-only; ignoring it")
            cfg.analyze_includes = False
            cfg.include_sdk = False

        # make sure SPDX directory exists, or create it if it doesn't
        if os.path.exists(cfg.spdx_dir):
            if not os.path.isdir(cfg.spdx_dir):
                self.err(f'SPDX output directory {cfg.spdx_dir} exists but is not a directory')
                return
            # directory exists, we're good
        else:
            # create the directory
            os.makedirs(cfg.spdx_dir, exist_ok=False)

        with contextlib.ExitStack() as stack:
            if args.modules_only:
                if args.meta:
                    cfg.meta_file = args.meta
                else:
                    tmpdir = stack.enter_context(tempfile.TemporaryDirectory())
                    cfg.meta_file = os.path.join(tmpdir, "zephyr.meta")
                    self.generate_module_meta(cfg.meta_file)

            if not make_spdx(cfg):
                self.die("Failed to create SPDX output")
