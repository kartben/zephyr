#!/usr/bin/env python3

# Copyright (c) 2025
# SPDX-License-Identifier: Apache-2.0

"""Generate a JSON snapshot of boards per SOC vendor for a given Zephyr tree.

Used by run_historical_snapshots.py to collect data from git worktrees.
Supports both strict (list_boards) and lenient parsing for historical commits
where board/soc schema may have evolved.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

try:
    from yaml import CSafeLoader as SafeLoader
except ImportError:
    from yaml import SafeLoader

import yaml

VENDOR_MERGE = {'atmel': 'microchip'}


def normalize_vendor(v: str) -> str:
    return VENDOR_MERGE.get(v, v)


def get_vendor_from_soc_path(soc_folder: str) -> str:
    path = Path(soc_folder)
    try:
        idx = path.parts.index('soc')
        if idx + 1 < len(path.parts):
            return normalize_vendor(path.parts[idx + 1].lower())
    except (ValueError, IndexError):
        pass
    return '(unknown)'


def _build_soc_to_vendor_from_soc_tree(zephyr_base: Path) -> dict:
    """Scan soc/ tree and build soc_name -> vendor mapping from soc.yml files."""
    soc_to_vendor = {}
    soc_dir = zephyr_base / 'soc'
    if not soc_dir.is_dir():
        return soc_to_vendor
    for soc_yml in soc_dir.rglob('soc.yml'):
        try:
            with soc_yml.open('r', encoding='utf-8') as f:
                data = yaml.load(f.read(), Loader=SafeLoader)
        except Exception:
            continue
        # Vendor from path: soc/<vendor>/...
        try:
            rel = soc_yml.relative_to(soc_dir)
            vendor = normalize_vendor(rel.parts[0].lower())
        except Exception:
            continue
        # Extract soc names from family/series/socs or top-level socs
        def collect_socs(obj):
            if isinstance(obj, dict):
                if 'socs' in obj:
                    for s in obj.get('socs', []) or []:
                        if isinstance(s, dict):
                            name = s.get('name') or s.get('extend')
                            if name:
                                soc_to_vendor[name] = vendor
                        elif isinstance(s, str):
                            soc_to_vendor[s] = vendor
                for k in ('series', 'family'):
                    if k in obj:
                        collect_socs(obj[k])
            elif isinstance(obj, list):
                for item in obj:
                    collect_socs(item)
        collect_socs(data)
    return soc_to_vendor


def _lenient_load_boards(zephyr_base: Path, soc_to_vendor: dict) -> list[tuple[str, str, list[str]]]:
    """Parse board.yml files without schema validation. Returns [(board_name, vendor, [soc_names])]."""
    boards_data = []
    boards_dir = zephyr_base / 'boards'
    if not boards_dir.is_dir():
        return boards_data
    for board_yml in boards_dir.rglob('board.yml'):
        try:
            with board_yml.open('r', encoding='utf-8') as f:
                data = yaml.load(f.read(), Loader=SafeLoader)
        except Exception:
            continue
        if not data:
            continue
        # Handle both 'board' and 'boards'
        entries = data.get('boards') or ([data.get('board')] if data.get('board') else [])
        for entry in entries:
            if not entry or not isinstance(entry, dict) or 'name' not in entry:
                continue
            if 'extend' in entry:
                continue
            name = entry['name']
            # Vendor from YAML or from path (boards/<vendor>/...)
            vendor_raw = entry.get('vendor')
            if not vendor_raw:
                try:
                    rel = board_yml.relative_to(boards_dir)
                    vendor_raw = rel.parts[0] if rel.parts else ''
                except Exception:
                    vendor_raw = ''
            vendor = normalize_vendor(str(vendor_raw).lower())
            socs_raw = entry.get('socs') or []
            soc_names = []
            if isinstance(socs_raw, list):
                for s in socs_raw:
                    if isinstance(s, dict) and 'name' in s:
                        soc_names.append(s['name'])
                    elif isinstance(s, str):
                        soc_names.append(s)
            elif isinstance(socs_raw, dict):
                for s in socs_raw.values() if isinstance(socs_raw, dict) else []:
                    if isinstance(s, dict) and 'name' in s:
                        soc_names.append(s['name'])
            boards_data.append((name, vendor, soc_names))
    return boards_data


def run_snapshot_lenient(zephyr_base: Path) -> dict:
    """Lenient snapshot: parse board.yml and soc/ without schema validation."""
    zephyr_base = Path(zephyr_base).resolve()
    if not (zephyr_base / 'boards').is_dir():
        raise SystemExit(f'ERROR: {zephyr_base} does not appear to be Zephyr base')

    soc_to_vendor = _build_soc_to_vendor_from_soc_tree(zephyr_base)
    boards_data = _lenient_load_boards(zephyr_base, soc_to_vendor)

    vendor_data = {}
    seen_boards = set()
    for board_name, board_vendor, soc_names in boards_data:
        if board_name in seen_boards:
            continue
        seen_boards.add(board_name)
        seen_soc_vendors = set()
        for soc_name in soc_names:
            soc_vendor = soc_to_vendor.get(soc_name, '(unknown)')
            if soc_vendor in seen_soc_vendors:
                continue
            seen_soc_vendors.add(soc_vendor)
            if soc_vendor not in vendor_data:
                vendor_data[soc_vendor] = {'vendor_boards': set(), 'community_boards': set()}
            bucket = vendor_data[soc_vendor]
            if board_vendor == soc_vendor:
                bucket['vendor_boards'].add(board_name)
            else:
                bucket['community_boards'].add(board_name)

    result = {}
    for v, data in vendor_data.items():
        result[v] = {
            'vendor': len(data['vendor_boards']),
            'community': len(data['community_boards']),
            'total': len(data['vendor_boards'] | data['community_boards']),
        }
    return {'total_boards': len(seen_boards), 'vendors': result}


def run_snapshot(zephyr_base: Path) -> dict:
    """Run analysis and return JSON-serializable snapshot data."""
    zephyr_base = Path(zephyr_base).resolve()
    if not (zephyr_base / 'boards').is_dir():
        raise SystemExit(f'ERROR: {zephyr_base} does not appear to be Zephyr base')

    # Try strict path first (list_boards + list_hardware)
    try:
        import list_boards
        import list_hardware
        import zephyr_module

        def get_args(zb):
            module_settings = {
                'arch_root': [zb], 'board_root': [zb], 'soc_root': [zb],
            }
            try:
                for module in zephyr_module.parse_modules(zb):
                    for key in module_settings:
                        root = module.meta.get('build', {}).get('settings', {}).get(key)
                        if root is not None:
                            module_settings[key].append(Path(module.project) / root)
            except Exception:
                pass
            return argparse.Namespace(
                arch_roots=module_settings['arch_root'],
                board_roots=module_settings['board_root'],
                soc_roots=module_settings['soc_root'],
                board=None, board_dir=[],
            )

        root_args = get_args(zephyr_base)
        systems = list_hardware.find_v2_systems(root_args)
        boards = list_boards.find_v2_boards(root_args)

        def get_board_vendor(b):
            raw = b.vendor or (b.dir.parent.name if hasattr(b, 'dir') else None)
            return normalize_vendor(str(raw).lower()) if raw else '(unknown)'

        vendor_data = {}
        for board in boards.values():
            board_vendor = get_board_vendor(board)
            seen_vendors = set()
            for soc in board.socs:
                try:
                    hw_soc = systems.get_soc(soc.name)
                except SystemExit:
                    continue
                soc_vendor = get_vendor_from_soc_path(hw_soc.folder[0])
                if soc_vendor in seen_vendors:
                    continue
                seen_vendors.add(soc_vendor)
                if soc_vendor not in vendor_data:
                    vendor_data[soc_vendor] = {'vendor_boards': set(), 'community_boards': set()}
                bucket = vendor_data[soc_vendor]
                if board_vendor == soc_vendor:
                    bucket['vendor_boards'].add(board.name)
                else:
                    bucket['community_boards'].add(board.name)

        result = {}
        for v, data in vendor_data.items():
            result[v] = {
                'vendor': len(data['vendor_boards']),
                'community': len(data['community_boards']),
                'total': len(data['vendor_boards'] | data['community_boards']),
            }
        return {'total_boards': len(boards), 'vendors': result}

    except (SystemExit, Exception):
        pass  # Fall through to lenient

    return run_snapshot_lenient(zephyr_base)


def main():
    parser = argparse.ArgumentParser(description='Snapshot boards per SOC vendor as JSON')
    parser.add_argument('--zephyr-base', type=Path, default=Path(__file__).parents[1])
    parser.add_argument('-o', '--output', type=Path, help='Write to file instead of stdout')
    args = parser.parse_args()

    data = run_snapshot(args.zephyr_base)
    json_str = json.dumps(data, indent=2)
    if args.output:
        args.output.write_text(json_str)
    else:
        print(json_str)


if __name__ == '__main__':
    main()
