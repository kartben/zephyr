#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Generate a JSON devicetree hardware catalog for Zephyr boards."""

import argparse
import datetime
import json
import logging
import shutil
import sys
import tempfile
from pathlib import Path

ZEPHYR_BASE = Path(__file__).resolve().parents[2]

sys.path.insert(0, str(ZEPHYR_BASE / "scripts" / "pylib" / "build_helpers"))

from board_catalog import (  # noqa: E402
    DeviceTreeUtils,
    ZEPHYR_BASE as BOARD_CATALOG_BASE,
    gather_board_build_info,
    get_binding_type_from_path,
    run_twister_cmake_only,
)

logger = logging.getLogger(__name__)


def _node_location(node) -> str:
    """Return 'board' or 'soc' depending on the DTS source location."""

    if not node.filename:
        return "soc"

    node_path = Path(node.filename)
    if node_path.is_relative_to(BOARD_CATALOG_BASE):
        rel_path = node_path.relative_to(BOARD_CATALOG_BASE)
        if rel_path.parts[0] == "boards":
            return "board"

    return "soc"


def build_catalog(board_devicetrees: dict) -> dict:
    """Build the DTS catalog from EDT objects harvested by Twister."""

    boards_db = {}
    compatibles_db = {}

    for board_name, board_targets in board_devicetrees.items():
        board_entry = {"targets": {}}

        for board_target, edt in board_targets.items():
            hardware = {}
            target_compatibles = set()

            for node in edt.nodes:
                if node.binding_path is None or node.matching_compat is None:
                    continue

                if node.matching_compat.startswith("zephyr,") and board_name != "native_sim":
                    continue

                compat = node.matching_compat
                binding_type = get_binding_type_from_path(Path(node.binding_path))
                target_compatibles.add(compat)

                filename = node.filename or ""
                if filename and Path(filename).is_relative_to(BOARD_CATALOG_BASE):
                    filename = Path(filename).relative_to(BOARD_CATALOG_BASE).as_posix()

                compat_entry = hardware.setdefault(binding_type, {}).get(compat)
                if compat_entry is None:
                    compat_entry = {
                        "description": DeviceTreeUtils.get_cached_description(node),
                        "title": node.title or "",
                        "locations": set(),
                        "okay": False,
                        "dts_sources": [],
                    }
                    hardware.setdefault(binding_type, {})[compat] = compat_entry

                compat_entry["locations"].add(_node_location(node))
                compat_entry["okay"] = compat_entry["okay"] or node.status == "okay"
                compat_entry["dts_sources"].append(
                    {
                        "file": filename,
                        "line": node.lineno,
                    }
                )

                if compat not in compatibles_db:
                    compatibles_db[compat] = {
                        "description": DeviceTreeUtils.get_cached_description(node),
                        "title": node.title or "",
                        "binding_type": binding_type,
                        "boards": [],
                    }

                if board_name not in compatibles_db[compat]["boards"]:
                    compatibles_db[compat]["boards"].append(board_name)

            for compat_map in hardware.values():
                for compat_entry in compat_map.values():
                    compat_entry["locations"] = sorted(compat_entry["locations"])

            board_entry["targets"][board_target] = {
                "compatibles": sorted(target_compatibles),
                "hardware": hardware,
            }

        boards_db[board_name] = board_entry

    for compat_entry in compatibles_db.values():
        compat_entry["boards"].sort()

    return {
        "generated_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "zephyr_base": str(BOARD_CATALOG_BASE),
        "boards": boards_db,
        "compatibles": compatibles_db,
    }


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description=__doc__,
        allow_abbrev=False,
    )
    parser.add_argument(
        "-o",
        "--output",
        required=True,
        metavar="FILE",
        help="Path for the output JSON database.",
    )
    parser.add_argument(
        "--twister-outdir",
        metavar="DIR",
        help=(
            "Directory to use for twister output. Defaults to a temporary directory "
            "that is deleted after the run."
        ),
    )
    parser.add_argument(
        "--skip-twister",
        action="store_true",
        help=(
            "Skip the twister cmake-only step and use whatever build artifacts already "
            "exist in --twister-outdir."
        ),
    )
    parser.add_argument(
        "--vendor",
        action="append",
        dest="vendor_filter",
        default=[],
        metavar="VENDOR",
        help="Restrict to boards from this vendor. May be repeated.",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Enable verbose logging.",
    )
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(levelname)s: %(message)s",
    )

    if args.skip_twister and not args.twister_outdir:
        logger.error("--skip-twister requires --twister-outdir")
        return 1

    tmp_dir = None

    try:
        if args.twister_outdir:
            twister_outdir = Path(args.twister_outdir)
            twister_outdir.mkdir(parents=True, exist_ok=True)
        else:
            tmp_dir = tempfile.mkdtemp(prefix="zephyr-dts-catalog-")
            twister_outdir = Path(tmp_dir)

        if not args.skip_twister:
            run_twister_cmake_only(twister_outdir, args.vendor_filter)

        board_devicetrees, _ = gather_board_build_info(twister_outdir)
        catalog = build_catalog(board_devicetrees)

        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with open(output_path, "w", encoding="utf-8") as output_file:
            json.dump(catalog, output_file, indent=2)

        logger.info(
            "Wrote %s (%d boards, %d compatibles)",
            output_path,
            len(catalog["boards"]),
            len(catalog["compatibles"]),
        )
    finally:
        if tmp_dir is not None:
            shutil.rmtree(tmp_dir, ignore_errors=True)

    return 0


if __name__ == "__main__":
    sys.exit(main())
