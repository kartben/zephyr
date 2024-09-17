# Copyright (c) 2024 The Linux Foundation
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import logging
import os
import sys
from collections import namedtuple
from pathlib import Path

import list_boards, list_shields
import pykwalify
import yaml
import zephyr_module
from gen_devicetree_rest import VndLookup

ZEPHYR_BASE = Path(__file__).parents[2]


def guess_file_from_patterns(directory, name, extensions, patterns):
    """
    Generalized utility function to guess a file based on given patterns and extensions.
    """
    for pattern in patterns:
        for ext in extensions:
            matching_file = next(directory.glob(pattern.format(name=name, ext=ext)), None)
            if matching_file:
                return matching_file
    return None


def guess_image(board_or_shield):
    img_exts = ["jpg", "jpeg", "webp", "png"]
    patterns = [
        "**/{name}.{ext}",
        "**/*{name}*.{ext}",
        "**/*.{ext}",
    ]
    img_file = guess_file_from_patterns(
        board_or_shield.dir, board_or_shield.name, img_exts, patterns
    )
    return (Path("../_images") / img_file.name).as_posix() if img_file else ""


def guess_doc_page(board_or_shield):
    patterns = [
        "{name}.rst",
        "index.rst",
        "*.rst",
    ]
    doc_file = guess_file_from_patterns(board_or_shield.dir, board_or_shield.name, [""], patterns)
    return doc_file.relative_to(ZEPHYR_BASE).as_posix() if doc_file else None


def main():
    """
    Generate a board_catalog.json file that contains information about all the boards in the Zephyr
    tree.
    """

    logging.basicConfig(level=logging.INFO)
    logging.info("Generating board catalog...")

    pykwalify.init_logging(1)

    parser = argparse.ArgumentParser(description="Generate boards catalog", allow_abbrev=False)
    # right now, turbo mode is a no-op, but it will be used in the future
    parser.add_argument("--turbo-mode", action="store_true", help="Enable turbo mode")
    parser.add_argument("out_dir", help="output files are generated here")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    # Load the vendor lookup table
    vnd_lookup = VndLookup(ZEPHYR_BASE / "dts/bindings/vendor-prefixes.txt", [])

    module_settings = {
        "arch_root": [ZEPHYR_BASE],
        "board_root": [ZEPHYR_BASE],
        "soc_root": [ZEPHYR_BASE],
    }

    for module in zephyr_module.parse_modules(ZEPHYR_BASE):
        for key in module_settings:
            root = module.meta.get("build", {}).get("settings", {}).get(key)
            if root is not None:
                module_settings[key].append(Path(module.project) / root)

    Args = namedtuple("args", ["arch_roots", "board_roots", "soc_roots", "board_dir", "board"])
    args_find_boards = Args(
        arch_roots=module_settings["arch_root"],
        board_roots=module_settings["board_root"],
        soc_roots=module_settings["soc_root"],
        board_dir=ZEPHYR_BASE / "boards",
        board=None,
    )

    boards = list_boards.find_v2_boards(args_find_boards)

    board_catalog = {}

    for board in boards:
        # board.vendor is often incorrect, instead, deduce vendor from containing folder
        # TODO: would be better to actually fix the vendor field in the board.yml files... :)
        for folder in board.dir.parents:
            if vnd_lookup.vnd2vendor.get(folder.name):
                vendor = folder.name
                break

        # look for all the twister files for this board and use them to figure out all the
        # supported architectures
        archs = set()
        pattern = f"{board.name}*.yaml"
        for twister_file in board.dir.glob(pattern):
            try:
                with open(twister_file, "r") as f:
                    board_data = yaml.safe_load(f)
                    archs.add(board_data.get("arch"))
            except Exception as e:
                pass

        # If full commercial name of the board is not available through board.full_name, we look for
        # the title of the board's documentation page. (we try with board.dir/doc/index.rst first,
        # then board.dir/doc/board_name.rst, then any other rst file in the doc folder that's not
        # either of the two previous ones)
        # /!\ This is a temporary solution until #79425 is resolved
        full_name = None
        doc_page = None

        patterns = [
            f"{board.name}.rst",
            "index.rst",
            "*.rst",
        ]

        if not board.full_name:
            for pattern in patterns:
                rst_file = f"**/{pattern}"
                for file in board.dir.glob(rst_file):
                    with open(file, "r") as f:
                        lines = f.readlines()
                        for i, line in enumerate(lines):
                            if line.startswith("#"):
                                full_name = lines[i - 1].strip()
                                doc_page = file
                                break
                    if full_name:
                        break

        if not full_name:
            print(f"Could not find doc page for {board.name}")
            full_name = board.name

        board_catalog[board.name] = {
            "type": "board",
            "dir": board.dir.relative_to(ZEPHYR_BASE).as_posix(),
            "full_name": full_name,
            "doc_page": doc_page.relative_to(ZEPHYR_BASE).as_posix() if doc_page else None,
            "vendor": vendor,
            "archs": list(archs),
            "image": guess_image(board),
        }

    Args = namedtuple("args", ["board_roots"])
    args_find_shields = Args(board_roots=module_settings["board_root"])
    shields = list_shields.find_shields(args_find_shields)
    for shield in shields:
        # try to guess the vendor of the shield i.e. does the section of the name before the first
        # underscore match a vendor prefix?
        vendor = None
        for part in shield.name.split("_"):
            if vnd_lookup.vnd2vendor.get(part):
                vendor = part
                break

        # guess doc page
        doc_page = None
        patterns = [
            f"{shield.name}.rst",
            "index.rst",
            "*.rst",
        ]
        for pattern in patterns:
            rst_file = f"**/{pattern}"
            for file in shield.dir.glob(rst_file):
                doc_page = file
                break

        # guess shield name
        full_name = None
        if doc_page:
            print(f"Guessing full name for {shield.name} from {doc_page}")
            with open(doc_page, "r") as f:
                lines = f.readlines()
                for i, line in enumerate(lines):
                    if line.startswith("#"):
                        full_name = lines[i - 1].strip()
                        break

        board_catalog[shield.name] = {
            "type": "shield",
            "dir": shield.dir.relative_to(ZEPHYR_BASE).as_posix(),
            "full_name": full_name or shield.name,
            "doc_page": doc_page.relative_to(ZEPHYR_BASE).as_posix() if doc_page else None,
            "vendor": vendor,
            "archs": None,
            "image": guess_image(shield),
        }

    with open(f"{args.out_dir}/board_catalog.json", "w") as f:
        json.dump({"boards": board_catalog, "vendors": vnd_lookup.vnd2vendor}, f, indent=2)


if __name__ == "__main__":
    main()
    sys.exit(0)
