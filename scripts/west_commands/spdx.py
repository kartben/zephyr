# Copyright (c) 2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import logging
import os
import sys
import uuid

from west.commands import WestCommand

from build_helpers import forward_logging_to_west

script_dir = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
sys.path.insert(0, os.path.join(script_dir, "pylib/"))
from zspdx.sbom import SBOMConfig, make_spdx, setup_cmake_query  # noqa: E402
from zspdx.version import SPDX_VERSION_2_3, SUPPORTED_SPDX_VERSIONS, parse  # noqa: E402

SPDX_DESCRIPTION = """\
This command creates an SPDX bill of materials following the completion
of a Zephyr build.

Prior to the build, an empty file must be created at
BUILDDIR/.cmake/api/v1/query/codemodel-v2 in order to enable
the CMake file-based API, which the SPDX command relies upon.
This can be done by calling `west spdx --init` prior to
calling `west build`."""

# Version recorded for the generated SBOM documents when the caller does not provide
# one. The CISA SBOM minimum elements recommend that an SBOM following them starts at
# major version 1.
DEFAULT_SBOM_VERSION = "1.0.0"


class ZephyrSpdx(WestCommand):
    def __init__(self):
        super().__init__('spdx', '', description=SPDX_DESCRIPTION)

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(self.name, description=self.description)

        # If you update these options, make sure to keep the docs in
        # doc/guides/west/zephyr-cmds.rst up to date.
        parser.add_argument(
            '-i', '--init', action="store_true", help="initialize CMake file-based API"
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
            '--sbom-author',
            help="organization generating this SBOM, recorded as the SPDX creator "
            "(default: west config zephyr.spdx.author, else the organization the Zephyr "
            "repository was fetched from)",
        )
        parser.add_argument(
            '--supplier',
            help="organization distributing the software described by this SBOM, recorded "
            "as the supplier of every package (default: west config zephyr.spdx.supplier, "
            "else the organization each repository was fetched from)",
        )
        parser.add_argument(
            '--originator',
            help="organization to credit as the producer of components with no upstream "
            "repository, such as the application sources and the build artifacts "
            "(default: west config zephyr.spdx.originator)",
        )
        parser.add_argument(
            '--sbom-version',
            default=DEFAULT_SBOM_VERSION,
            help="version of the SBOM document itself, as opposed to the version of the "
            f"software it describes (default: {DEFAULT_SBOM_VERSION})",
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
        self.dbg("  --sbom-author is", args.sbom_author)
        self.dbg("  --supplier is", args.supplier)
        self.dbg("  --originator is", args.originator)
        self.dbg("  --sbom-version is", args.sbom_version)

        if args.init:
            self.do_run_init(args)
        else:
            self.do_run_spdx(args)

    def do_run_init(self, args):
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

    def provenance_option(self, value, config_name):
        """Return a provenance option, falling back to `zephyr.spdx.<config_name>`."""
        if value:
            return value
        return self.config.get(f'zephyr.spdx.{config_name}', default='')

    def do_run_spdx(self, args):
        if not args.build_dir:
            self.die("Build directory not specified; call `west spdx --build-dir=BUILD_DIR`")

        # create the SPDX files
        cfg = SBOMConfig()
        cfg.build_dir = args.build_dir
        try:
            version_obj = parse(args.spdx_version)
        except Exception:
            self.die(f"Invalid SPDX version: {args.spdx_version}")
        cfg.spdx_version = version_obj
        if args.namespace_prefix:
            cfg.namespace_prefix = args.namespace_prefix
        else:
            # create default namespace according to SPDX spec
            # note that this is intentionally _not_ an actual URL where
            # this document will be stored
            cfg.namespace_prefix = f"http://spdx.org/spdxdocs/zephyr-{str(uuid.uuid4())}"
        if args.spdx_dir:
            cfg.spdx_dir = args.spdx_dir
        else:
            cfg.spdx_dir = os.path.join(args.build_dir, "spdx")
        if args.analyze_includes:
            cfg.analyze_includes = True
        if args.include_sdk:
            cfg.include_sdk = True

        # provenance of the SBOM and of what it describes: the command line wins, then
        # west config, so that a downstream or fork workspace can be configured once
        # instead of on every invocation
        cfg.sbom_author = self.provenance_option(args.sbom_author, 'author')
        cfg.supplier = self.provenance_option(args.supplier, 'supplier')
        cfg.originator = self.provenance_option(args.originator, 'originator')
        cfg.sbom_version = args.sbom_version

        # make sure SPDX directory exists, or create it if it doesn't
        if os.path.exists(cfg.spdx_dir):
            if not os.path.isdir(cfg.spdx_dir):
                self.err(f'SPDX output directory {cfg.spdx_dir} exists but is not a directory')
                return
            # directory exists, we're good
        else:
            # create the directory
            os.makedirs(cfg.spdx_dir, exist_ok=False)

        if not make_spdx(cfg):
            self.die("Failed to create SPDX output")
