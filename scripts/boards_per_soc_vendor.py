#!/usr/bin/env python3

# Copyright (c) 2025
# SPDX-License-Identifier: Apache-2.0

"""Report a breakdown of boards per SOC vendor."""

import argparse
import sys
from pathlib import Path

# Add scripts dir to path for imports
sys.path.insert(0, str(Path(__file__).parent))

import list_boards
import list_hardware
import zephyr_module


def get_vendor_from_soc_path(soc_folder: str) -> str:
    """Extract vendor from SOC folder path (first directory under soc/)."""
    path = Path(soc_folder)
    try:
        idx = path.parts.index('soc')
        if idx + 1 < len(path.parts):
            return path.parts[idx + 1]
    except (ValueError, IndexError):
        pass
    return '(unknown)'


def get_args(zephyr_base: Path):
    """Build args with roots from ZEPHYR_BASE and zephyr modules."""
    module_settings = {
        'arch_root': [zephyr_base],
        'board_root': [zephyr_base],
        'soc_root': [zephyr_base],
    }

    for module in zephyr_module.parse_modules(zephyr_base):
        for key in module_settings:
            root = module.meta.get('build', {}).get('settings', {}).get(key)
            if root is not None:
                module_settings[key].append(Path(module.project) / root)

    return argparse.Namespace(
        arch_roots=module_settings['arch_root'],
        board_roots=module_settings['board_root'],
        soc_roots=module_settings['soc_root'],
        board=None,
        board_dir=[],
    )


def main():
    parser = argparse.ArgumentParser(
        description='Show breakdown of boards per SOC vendor'
    )
    parser.add_argument(
        '--id', dest='show_ids', action='store_true',
        help='show board names under each vendor'
    )
    parser.add_argument(
        '--zephyr-base', type=Path, default=Path(__file__).parents[1],
        help='Zephyr base directory'
    )
    args = parser.parse_args()

    zephyr_base = args.zephyr_base.resolve()
    if not (zephyr_base / 'boards').is_dir():
        sys.exit(f'ERROR: {zephyr_base} does not appear to be Zephyr base')

    root_args = get_args(zephyr_base)
    systems = list_hardware.find_v2_systems(root_args)
    boards = list_boards.find_v2_boards(root_args)

    # Build: vendor -> list of boards
    vendor_to_boards = {}
    for board in boards.values():
        for soc in board.socs:
            try:
                hw_soc = systems.get_soc(soc.name)
            except SystemExit:
                continue
            # Vendor from first folder (soc.yml location)
            vendor = get_vendor_from_soc_path(hw_soc.folder[0])
            if vendor not in vendor_to_boards:
                vendor_to_boards[vendor] = []
            if board not in vendor_to_boards[vendor]:
                vendor_to_boards[vendor].append(board)

    # Sort by count descending, then by vendor name
    for vendor in sorted(
        vendor_to_boards.keys(),
        key=lambda v: (-len(vendor_to_boards[v]), v)
    ):
        boards_list = vendor_to_boards[vendor]
        print(f'{vendor}: {len(boards_list)}')
        if args.show_ids:
            for b in sorted(boards_list, key=lambda x: x.name):
                print(f'  {b.name}')

    print(f'\nTotal: {len(boards)} boards across {len(vendor_to_boards)} SOC vendors')


if __name__ == '__main__':
    main()
