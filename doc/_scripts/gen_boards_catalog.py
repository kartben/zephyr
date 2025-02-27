# Copyright (c) 2024-2025 The Linux Foundation
# SPDX-License-Identifier: Apache-2.0

import logging
import os
import pickle
import re
import subprocess
import sys
from collections import namedtuple
from pathlib import Path

import list_boards
import list_hardware
import yaml
import zephyr_module
from gen_devicetree_rest import VndLookup

ZEPHYR_BASE = Path(__file__).parents[2]
ZEPHYR_BINDINGS = ZEPHYR_BASE / "dts/bindings"
EDT_PICKLE_PATH = "zephyr/edt.pickle"

logger = logging.getLogger(__name__)


class DeviceTreeUtils:
    _compat_description_cache = {}

    @classmethod
    def get_first_sentence(cls, text):
        """Extract the first sentence from a text block (typically a node description).

        Args:
            text: The text to extract the first sentence from.

        Returns:
            The first sentence found in the text, or the entire text if no sentence
            boundary is found.
        """
        # Split the text into lines
        lines = text.splitlines()

        # Trim leading and trailing whitespace from each line and ignore completely blank lines
        lines = [line.strip() for line in lines]

        if not lines:
            return ""

        # Case 1: Single line followed by blank line(s) or end of text
        if len(lines) == 1 or (len(lines) > 1 and lines[1] == ""):
            first_line = lines[0]
            # Check for the first period
            period_index = first_line.find(".")
            # If there's a period, return up to the period; otherwise, return the full line
            return first_line[: period_index + 1] if period_index != -1 else first_line

        # Case 2: Multiple contiguous lines, treat as a block
        block = " ".join(lines)
        period_index = block.find(".")
        # If there's a period, return up to the period; otherwise, return the full block
        return block[: period_index + 1] if period_index != -1 else block

    @classmethod
    def get_cached_description(cls, node):
        """Get the cached description for a devicetree node.

        Args:
            node: A devicetree node object with matching_compat and description attributes.

        Returns:
            The cached description for the node's compatible, creating it if needed.
        """
        return cls._compat_description_cache.setdefault(
            node.matching_compat,
            cls.get_first_sentence(node.description)
        )


def guess_file_from_patterns(directory, patterns, name, extensions):
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
        board_or_shield.dir, patterns, board_or_shield.name, img_exts
    )

    return (img_file.relative_to(ZEPHYR_BASE)).as_posix() if img_file else None


def guess_doc_page(board_or_shield):
    patterns = [
        "doc/index.{ext}",
        "**/{name}.{ext}",
        "**/*{name}*.{ext}",
        "**/*.{ext}",
    ]
    doc_file = guess_file_from_patterns(
        board_or_shield.dir, patterns, board_or_shield.name, ["rst"]
    )
    return doc_file


def gather_board_devicetrees(twister_out_dir):
    """Gather EDT objects for each board from twister output directory.

    Args:
        twister_out_dir: Path object pointing to twister output directory

    Returns:
        A tuple containing:
        1. A dictionary mapping board names to a dictionary of board targets and their EDT objects.
           The structure is: {board_name: {board_target: edt_object}}
        2. A dictionary mapping board names to a dictionary of board targets and their runners info.
           The structure is: {board_name: {board_target: runners_info}}
    """
    board_devicetrees = {}
    board_runners = {}

    if not twister_out_dir.exists():
        return board_devicetrees, board_runners

    # Find all build_info.yml files in twister-out
    build_info_files = list(twister_out_dir.glob("*/**/build_info.yml"))

    for build_info_file in build_info_files:
        # Look for corresponding zephyr.dts
        edt_pickle_file = build_info_file.parent / EDT_PICKLE_PATH
        runners_yaml_file = build_info_file.parent / "zephyr/runners.yaml"

        try:
            with open(build_info_file) as f:
                build_info = yaml.safe_load(f)
                board_info = build_info.get('cmake', {}).get('board', {})
                board_name = board_info.get('name')
                qualifier = board_info.get('qualifiers', '')
                revision = board_info.get('revision', '')

                board_target = board_name
                if qualifier:
                    board_target = f"{board_name}/{qualifier}"
                if revision:
                    board_target = f"{board_target}@{revision}"

                # Process EDT pickle file if it exists
                if edt_pickle_file.exists():
                    try:
                        with open(edt_pickle_file, 'rb') as f:
                            edt = pickle.load(f)
                            board_devicetrees.setdefault(board_name, {})[board_target] = edt
                    except Exception as e:
                        logger.error(f"Error processing EDT pickle file {edt_pickle_file}: {e}")

                # Process runners.yaml file if it exists
                if runners_yaml_file.exists():
                    try:
                        with open(runners_yaml_file) as f:
                            runners_info = yaml.safe_load(f)
                            board_runners.setdefault(board_name, {})[board_target] = runners_info
                    except Exception as e:
                        logger.error(f"Error processing runners.yaml file {runners_yaml_file}: {e}")

        except Exception as e:
            logger.error(f"Error processing build info file {build_info_file}: {e}")

    return board_devicetrees, board_runners


def get_runners_info(board_runners, board_name, board_target=None):
    """Get runners information for a specific board.

    Args:
        board_runners: Dictionary mapping board names to board targets and their runners info
        board_name: Name of the board
        board_target: Specific board target (optional)

    Returns:
        Dictionary containing runners information or None if not found
    """
    if board_name not in board_runners:
        return None

    if board_target is None:
        # Return the first available target if no specific target is requested
        return next(iter(board_runners[board_name].values()), None)

    return board_runners[board_name].get(board_target)


def run_twister_cmake_only(outdir):
    """Run twister in cmake-only mode to generate build info files.

    Args:
        outdir: Directory where twister should output its files
    """
    twister_cmd = [
        sys.executable,
        f"{ZEPHYR_BASE}/scripts/twister",
        "-T", "samples/hello_world/",
        "--all",
        "-M",
        "--keep-artifacts", "zephyr/edt.pickle",
        "--keep-artifacts", "zephyr/runners.yaml",
        "--cmake-only",
        "--outdir", str(outdir),
    ]

    minimal_env = {
        'PATH': os.environ.get('PATH', ''),
        'ZEPHYR_BASE': str(ZEPHYR_BASE),
        'HOME': os.environ.get('HOME', ''),
        'PYTHONPATH': os.environ.get('PYTHONPATH', '')
    }

    try:
        subprocess.run(twister_cmd, check=True, cwd=ZEPHYR_BASE, env=minimal_env)
    except subprocess.CalledProcessError as e:
        logger.warning(f"Failed to run Twister, list of hw features might be incomplete.\n{e}")


def get_catalog(generate_hw_features=False):
    """Get the board catalog.

    Args:
        generate_hw_features: If True, run twister to generate hardware features information.
    """
    import tempfile

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
        board_dir=[],
        board=None,
    )

    boards = list_boards.find_v2_boards(args_find_boards)
    systems = list_hardware.find_v2_systems(args_find_boards)
    board_catalog = {}
    board_devicetrees = {}
    board_runners = {}

    if generate_hw_features:
        logger.info("Running twister in cmake-only mode to get Devicetree files for all boards")
        with tempfile.TemporaryDirectory() as tmp_dir:
            run_twister_cmake_only(tmp_dir)
            board_devicetrees, board_runners = gather_board_devicetrees(Path(tmp_dir))
    else:
        logger.info("Skipping generation of supported hardware features.")

    for board in boards.values():
        # We could use board.vendor but it is often incorrect. Instead, deduce vendor from
        # containing folder. There are a few exceptions, like the "native" and "others" folders
        # which we know are not actual vendors so treat them as such.
        for folder in board.dir.parents:
            if folder.name in ["native", "others"]:
                vendor = "others"
                break
            elif vnd_lookup.vnd2vendor.get(folder.name):
                vendor = folder.name
                break

        socs = {soc.name for soc in board.socs}
        full_name = board.full_name or board.name
        doc_page = guess_doc_page(board)

        supported_features = {}

        # Use pre-gathered build info and DTS files
        if board.name in board_devicetrees:
            for board_target, edt in board_devicetrees[board.name].items():
                okay_nodes = [
                    node
                    for node in edt.nodes
                    if node.status == "okay" and node.matching_compat is not None
                ]

                target_features = {}
                for node in okay_nodes:
                    binding_path = Path(node.binding_path)
                    binding_type = (
                        binding_path.relative_to(ZEPHYR_BINDINGS).parts[0]
                        if binding_path.is_relative_to(ZEPHYR_BINDINGS)
                        else "misc"
                    )
                    description = DeviceTreeUtils.get_cached_description(node)
                    target_features.setdefault(binding_type, {}).setdefault(
                        node.matching_compat, description
                    )

                # Store features for this specific target
                supported_features[board_target] = target_features

        # Grab all the twister files for this board and use them to figure out all the archs it
        # supports.
        archs = set()
        pattern = f"{board.name}*.yaml"
        for twister_file in board.dir.glob(pattern):
            try:
                with open(twister_file) as f:
                    board_data = yaml.safe_load(f)
                    archs.add(board_data.get("arch"))
            except Exception as e:
                logger.error(f"Error parsing twister file {twister_file}: {e}")

        # Get list
        runners = set()
        board_cmake = board.dir / "board.cmake"
        if board_cmake.exists():
            with open(board_cmake) as f:
                for line in f:
                    match = re.search(r"include\((.*\.board\.cmake)\)", line)
                    if match:
                        runners.add(match.group(1).split("/")[-1].split(".")[0])

        # Check if we have runners information from twister output
        runners_info = None
        if board.name in board_runners:
            # Get the first available target's runners info
            runners_info = get_runners_info(board_runners, board.name)
            if runners_info and 'runners' in runners_info:
                # Add runners from the runners.yaml file
                runners.update(runners_info['runners'])

        board_catalog[board.name] = {
            "name": board.name,
            "full_name": full_name,
            "doc_page": doc_page.relative_to(ZEPHYR_BASE).as_posix() if doc_page else None,
            "vendor": vendor,
            "archs": list(archs),
            "socs": list(socs),
            "supported_features": supported_features,
            "image": guess_image(board),
            "runners": list(runners),
            # runners_info contains the full contents of runners.yaml if available,
            # including available runners, default runners for flash/debug/etc.,
            # and runner-specific configuration
            "runners_info": runners_info,  # Include the full runners info if available
        }

    socs_hierarchy = {}
    for soc in systems.get_socs():
        family = soc.family or "<no family>"
        series = soc.series or "<no series>"
        socs_hierarchy.setdefault(family, {}).setdefault(series, []).append(soc.name)

    return {
        "boards": board_catalog,
        "vendors": {**vnd_lookup.vnd2vendor, "others": "Other/Unknown"},
        "socs": socs_hierarchy,
    }
