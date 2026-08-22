#!/usr/bin/env python3

# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Resolve west projects required by a board, SoC, or shield.

This script is shared by the build system and the ``west modules``
extension command so that west remains optional. Hardware metadata
lives in :file:`soc.yml`, :file:`board.yml`, and :file:`shield.yml`.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

import yaml

try:
    from yaml import CSafeLoader as SafeLoader
except ImportError:
    from yaml import SafeLoader

import list_boards
import list_hardware
import list_shields

DEFAULTS_YML = Path(__file__).parent / 'modules-defaults.yml'


def sanitize_module_name(name: str) -> str:
    """Convert a west project / module name to the CMake/Kconfig form."""
    return re.sub('[^a-zA-Z0-9]', '_', name).upper()


def merge_unique(*groups):
    merged = []
    for group in groups:
        for name in group or []:
            if name not in merged:
                merged.append(name)
    return merged


def load_default_modules(defaults_file: Path | None = None) -> list[str]:
    path = defaults_file or DEFAULTS_YML
    if not path.is_file():
        return []
    with path.open('r', encoding='utf-8') as f:
        data = yaml.load(f.read(), Loader=SafeLoader) or {}
    return list(data.get('defaults', []))


@dataclass
class ModuleRequirement:
    name: str
    sources: list[str] = field(default_factory=list)

    @property
    def cmake_var(self) -> str:
        return f'ZEPHYR_{sanitize_module_name(self.name)}_MODULE_DIR'


@dataclass
class ResolvedModules:
    required: list[ModuleRequirement] = field(default_factory=list)

    @property
    def names(self) -> list[str]:
        return [req.name for req in self.required]

    def missing(self, present: set[str]) -> list[str]:
        present_sanitized = {sanitize_module_name(name) for name in present}
        missing = []
        for req in self.required:
            if sanitize_module_name(req.name) not in present_sanitized:
                missing.append(req.name)
        return missing


def _add_req(reqs: dict[str, ModuleRequirement], name: str, source: str):
    if name not in reqs:
        reqs[name] = ModuleRequirement(name)
    if source not in reqs[name].sources:
        reqs[name].sources.append(source)


def resolve_modules(board_name: str | None = None,
                    shields: list[str] | None = None,
                    board_roots: list[Path] | None = None,
                    soc_roots: list[Path] | None = None,
                    include_defaults: bool = True,
                    defaults_file: Path | None = None,
                    zephyr_base: Path | None = None) -> ResolvedModules:
    """Return required west projects for a board and optional shields."""
    zephyr_base = Path(zephyr_base) if zephyr_base else Path(__file__).resolve().parent.parent
    board_roots = list(board_roots or [zephyr_base])
    soc_roots = list(soc_roots or [zephyr_base])
    shields = list(shields or [])

    reqs: dict[str, ModuleRequirement] = {}

    if include_defaults:
        for name in load_default_modules(defaults_file):
            _add_req(reqs, name, 'defaults')

    if board_name:
        # BOARD may be a target like nucleo_f401re/stm32f401xe; hardware
        # metadata is keyed by the board name (first path component).
        lookup_name = board_name.split('/')[0].split('@')[0]
        args = argparse.Namespace(
            soc_roots=soc_roots,
            board_roots=board_roots,
            arch_roots=[],
            board=lookup_name,
            board_dir=[],
        )
        boards = list_boards.find_v2_boards(args)
        board = boards.get(lookup_name)
        if board is None:
            raise RuntimeError(
                f"Board '{lookup_name}' not found. Check --board / BOARD and board roots."
            )
        for name in board.modules:
            _add_req(reqs, name, f'board:{board.name}')
        for soc in board.socs:
            for name in soc.modules:
                _add_req(reqs, name, f'soc:{soc.name}')

    if shields:
        shield_args = argparse.Namespace(board_roots=board_roots)
        found = {s.name: s for s in list_shields.find_shields(shield_args)}
        for shield_name in shields:
            shield = found.get(shield_name)
            if shield is None:
                raise RuntimeError(
                    f"Shield '{shield_name}' not found. Check --shield / SHIELD and board roots."
                )
            for name in shield.modules:
                _add_req(reqs, name, f'shield:{shield.name}')

    return ResolvedModules(required=list(reqs.values()))


def list_declared_modules(board_roots: list[Path] | None = None,
                          soc_roots: list[Path] | None = None,
                          zephyr_base: Path | None = None) -> ResolvedModules:
    """Collect every module declared in hardware metadata (no defaults)."""
    zephyr_base = Path(zephyr_base) if zephyr_base else Path(__file__).resolve().parent.parent
    board_roots = list(board_roots or [zephyr_base])
    soc_roots = list(soc_roots or [zephyr_base])

    reqs: dict[str, ModuleRequirement] = {}

    systems = list_hardware.find_v2_systems(argparse.Namespace(soc_roots=soc_roots))
    for soc in systems.get_socs():
        for name in soc.modules:
            _add_req(reqs, name, f'soc:{soc.name}')

    args = argparse.Namespace(
        soc_roots=soc_roots,
        board_roots=board_roots,
        arch_roots=[],
        board=None,
        board_dir=[],
    )
    for board in list_boards.find_v2_boards(args).values():
        for name in board.modules:
            _add_req(reqs, name, f'board:{board.name}')

    for shield in list_shields.find_shields(argparse.Namespace(board_roots=board_roots)):
        for name in shield.modules:
            _add_req(reqs, name, f'shield:{shield.name}')

    return ResolvedModules(required=list(reqs.values()))


def add_args(parser):
    parser.add_argument('--board', default=None,
                        help='board name or board target to resolve')
    parser.add_argument('--shield', dest='shields', action='append', default=[],
                        help='shield name, may be given more than once')
    parser.add_argument('--board-root', dest='board_roots', default=[],
                        type=Path, action='append',
                        help='add a board root, may be given more than once')
    parser.add_argument('--soc-root', dest='soc_roots', default=[],
                        type=Path, action='append',
                        help='add a SoC root, may be given more than once')
    parser.add_argument('--zephyr-base', type=Path, default=None,
                        help='path to the zephyr repository')
    parser.add_argument('--defaults', dest='include_defaults',
                        action=argparse.BooleanOptionalAction, default=True,
                        help='include scripts/modules-defaults.yml (default: true)')
    parser.add_argument('--all-declared', action='store_true',
                        help='list every module declared in hardware metadata')
    parser.add_argument('--cmake', action='store_true',
                        help='print a CMake set() of required module names')
    parser.add_argument('--format', default='{name}',
                        help='Python format string; fields: name, sources, cmake_var')


def dump_modules(resolved: ResolvedModules, args):
    if args.cmake:
        names = ';'.join(resolved.names)
        print(f'set(ZEPHYR_REQUIRED_MODULES {names})')
        return

    for req in resolved.required:
        print(args.format.format(
            name=req.name,
            sources=','.join(req.sources),
            cmake_var=req.cmake_var,
        ))


def parse_args():
    parser = argparse.ArgumentParser(allow_abbrev=False)
    add_args(parser)
    return parser.parse_args()


def main():
    args = parse_args()
    zephyr_base = args.zephyr_base
    board_roots = args.board_roots or None
    soc_roots = args.soc_roots or None

    try:
        if args.all_declared:
            resolved = list_declared_modules(
                board_roots=board_roots,
                soc_roots=soc_roots,
                zephyr_base=zephyr_base,
            )
        else:
            resolved = resolve_modules(
                board_name=args.board,
                shields=args.shields,
                board_roots=board_roots,
                soc_roots=soc_roots,
                include_defaults=args.include_defaults,
                zephyr_base=zephyr_base,
            )
    except RuntimeError as e:
        print(f'ERROR: {e}', file=sys.stderr)
        sys.exit(1)

    dump_modules(resolved, args)


if __name__ == '__main__':
    main()
