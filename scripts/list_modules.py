#!/usr/bin/env python3

# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Resolve west projects required by a build.

Dependencies are declared where they live, not only on boards:

* SoC / board / shield YAML (``modules:``)
* Driver and subsystem Kconfig (``depends on ZEPHYR_<NAME>_MODULE``)
* Devicetree bindings (optional ``modules:``)
* Application ``sample.yaml`` / ``tests.yaml`` (existing Twister ``modules:``)

A board target is just a *query*: the resolver walks the SoC, the
devicetree (which implies drivers), shields, and the application.
West is optional; this script is shared with CMake.
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

ZEPHYR_MODULE_RE = re.compile(r'ZEPHYR_([A-Z0-9_]+)_MODULE(?:_BLOBS)?\b')
DT_HAS_RE = re.compile(r'DT_HAS_([A-Z0-9_]+)_ENABLED\b')
CONFIG_HEADER_RE = re.compile(r'^(?:menuconfig|config)\s+([A-Z0-9_]+)\s*$')
COMPAT_ASSIGN_RE = re.compile(r'\bcompatible\s*=\s*([^;]+);', re.S)
COMPAT_STRING_RE = re.compile(r'"([^"]+)"')
CONF_ENABLE_RE = re.compile(r'^CONFIG_([A-Z0-9_]+)\s*=\s*y\s*$', re.M)
DTS_SUFFIXES = ('.dts', '.dtsi', '.overlay')


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


def _load_yaml(path: Path):
    with path.open('r', encoding='utf-8') as f:
        return yaml.load(f.read(), Loader=SafeLoader)


def load_manifest_project_names(zephyr_base: Path) -> dict[str, str]:
    """Map sanitized names to west.yml project names."""
    names: dict[str, str] = {}
    paths = [zephyr_base / 'west.yml']
    submanifests = zephyr_base / 'submanifests'
    if submanifests.is_dir():
        paths.extend(sorted(submanifests.glob('*.yaml')))
        paths.extend(sorted(submanifests.glob('*.yml')))
    for path in paths:
        if not path.is_file():
            continue
        try:
            data = _load_yaml(path) or {}
        except (OSError, yaml.YAMLError):
            continue
        for project in data.get('manifest', {}).get('projects', []):
            name = project.get('name')
            if name:
                names[sanitize_module_name(name)] = name
    return names


def module_name_from_kconfig_token(sanitized: str, project_names: dict[str, str]) -> str:
    if sanitized in project_names:
        return project_names[sanitized]
    return sanitized.lower()


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


@dataclass
class KconfigModuleDep:
    symbol: str
    modules: list[str]
    dt_tokens: list[str]
    path: str


def _add_req(reqs: dict[str, ModuleRequirement], name: str, source: str):
    if name not in reqs:
        reqs[name] = ModuleRequirement(name)
    if source not in reqs[name].sources:
        reqs[name].sources.append(source)


def _kconfig_join_continuations(text: str) -> str:
    return re.sub(r'\\\r?\n\s*', ' ', text)


def iter_kconfig_symbols(text: str):
    """Yield (symbol, depends_expr) from a Kconfig file body."""
    text = _kconfig_join_continuations(text)
    current = None
    depends: list[str] = []
    for line in text.splitlines():
        stripped = line.split('#', 1)[0].strip()
        if not stripped:
            continue
        header = CONFIG_HEADER_RE.match(stripped)
        if header:
            if current:
                yield current, ' '.join(depends)
            current = header.group(1)
            depends = []
            continue
        if current is None:
            continue
        if stripped.startswith('depends on '):
            depends.append(stripped[len('depends on '):])
        elif stripped in {'help', 'endmenu', 'endif', 'endchoice', 'source', 'rsource', 'osource'}:
            yield current, ' '.join(depends)
            current = None
            depends = []
    if current:
        yield current, ' '.join(depends)


def scan_kconfig_module_deps(roots: list[Path],
                             project_names: dict[str, str]) -> list[KconfigModuleDep]:
    deps: list[KconfigModuleDep] = []
    seen: set[tuple[str, str]] = set()
    kconfig_files: list[Path] = []
    for root in roots:
        if not root.is_dir():
            continue
        kconfig_files.extend(root.rglob('Kconfig'))
        kconfig_files.extend(root.rglob('Kconfig.*'))
    for path in kconfig_files:
        if not path.is_file():
            continue
        try:
            text = path.read_text(encoding='utf-8')
        except OSError:
            continue
        for symbol, expr in iter_kconfig_symbols(text):
            modules = []
            for match in ZEPHYR_MODULE_RE.finditer(expr):
                modules.append(module_name_from_kconfig_token(match.group(1), project_names))
            if not modules:
                continue
            dt_tokens = DT_HAS_RE.findall(expr)
            key = (symbol, path.as_posix())
            if key in seen:
                continue
            seen.add(key)
            deps.append(KconfigModuleDep(
                symbol=symbol,
                modules=merge_unique(modules),
                dt_tokens=dt_tokens,
                path=path.as_posix(),
            ))
    return deps


def scan_binding_modules(binding_roots: list[Path]) -> dict[str, list[str]]:
    """Map sanitized compatible → modules declared on the binding."""
    by_compat: dict[str, list[str]] = {}
    for root in binding_roots:
        if not root.is_dir():
            continue
        for path in root.rglob('*.yaml'):
            try:
                data = _load_yaml(path)
            except (OSError, yaml.YAMLError):
                continue
            if not isinstance(data, dict):
                continue
            compatible = data.get('compatible')
            modules = data.get('modules')
            if not compatible or not modules:
                continue
            if isinstance(compatible, str):
                compatibles = [compatible]
            else:
                compatibles = list(compatible)
            for compat in compatibles:
                token = sanitize_module_name(compat)
                by_compat[token] = merge_unique(by_compat.get(token, []), modules)
    return by_compat


def extract_compatibles(text: str) -> list[str]:
    found = []
    for assign in COMPAT_ASSIGN_RE.findall(text):
        for compat in COMPAT_STRING_RE.findall(assign):
            if compat not in found:
                found.append(compat)
    return found


def scan_dts_compatibles(directories: list[Path]) -> list[str]:
    found: list[str] = []
    for directory in directories:
        if not directory.is_dir():
            continue
        for path in directory.rglob('*'):
            if path.suffix not in DTS_SUFFIXES or not path.is_file():
                continue
            try:
                text = path.read_text(encoding='utf-8')
            except OSError:
                continue
            for compat in extract_compatibles(text):
                if compat not in found:
                    found.append(compat)
    return found


def collect_twister_modules(app_dir: Path) -> list[tuple[str, str]]:
    """Return (module, source) from sample.yaml / tests.yaml / testcase.yaml."""
    found: list[tuple[str, str]] = []
    for name in ('tests.yaml', 'sample.yaml', 'testcase.yaml'):
        path = app_dir / name
        if not path.is_file():
            continue
        try:
            data = _load_yaml(path)
        except (OSError, yaml.YAMLError):
            continue
        if not isinstance(data, dict):
            continue

        def walk(node, prefix):
            if isinstance(node, dict):
                modules = node.get('modules')
                if isinstance(modules, list):
                    for module in modules:
                        found.append((module, f'{prefix}:{path.name}'))
                for key, value in node.items():
                    if key != 'modules':
                        walk(value, prefix)
            elif isinstance(node, list):
                for item in node:
                    walk(item, prefix)

        walk(data, 'app')
    return found


def collect_conf_symbols(app_dir: Path) -> list[str]:
    symbols = []
    for path in [app_dir / 'prj.conf', *sorted(app_dir.glob('*.conf'))]:
        if not path.is_file():
            continue
        try:
            text = path.read_text(encoding='utf-8')
        except OSError:
            continue
        for symbol in CONF_ENABLE_RE.findall(text):
            if symbol not in symbols:
                symbols.append(symbol)
    return symbols


def _dts_directories_for_board(board, board_name: str) -> list[Path]:
    dirs = []
    directories = board.directories
    if not isinstance(directories, list):
        directories = [directories]
    dirs.extend(directories)
    return dirs


def resolve_modules(board_name: str | None = None,
                    shields: list[str] | None = None,
                    board_roots: list[Path] | None = None,
                    soc_roots: list[Path] | None = None,
                    include_defaults: bool = True,
                    defaults_file: Path | None = None,
                    zephyr_base: Path | None = None,
                    app_dir: Path | None = None,
                    kconfig_roots: list[Path] | None = None,
                    binding_roots: list[Path] | None = None) -> ResolvedModules:
    """Return required west projects for a build query."""
    zephyr_base = Path(zephyr_base) if zephyr_base else Path(__file__).resolve().parent.parent
    board_roots = list(board_roots or [zephyr_base])
    soc_roots = list(soc_roots or [zephyr_base])
    shields = list(shields or [])
    app_dir = Path(app_dir) if app_dir else None
    project_names = load_manifest_project_names(zephyr_base)

    reqs: dict[str, ModuleRequirement] = {}

    if include_defaults:
        for name in load_default_modules(defaults_file):
            _add_req(reqs, name, 'defaults')

    board = None
    dts_dirs: list[Path] = []

    if board_name:
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
        dts_dirs.extend(_dts_directories_for_board(board, lookup_name))

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
            dts_dirs.append(shield.dir)

    if app_dir:
        dts_dirs.append(app_dir)
        for name, source in collect_twister_modules(app_dir):
            _add_req(reqs, name, source)

    k_roots = list(kconfig_roots) if kconfig_roots is not None else [
        zephyr_base / 'drivers',
        zephyr_base / 'subsys',
        zephyr_base / 'lib',
        zephyr_base / 'modules',
    ]
    b_roots = list(binding_roots) if binding_roots is not None else [
        zephyr_base / 'dts' / 'bindings',
    ]

    kconfig_deps = scan_kconfig_module_deps(k_roots, project_names)
    binding_modules = scan_binding_modules(b_roots)
    compatibles = scan_dts_compatibles(dts_dirs)
    compat_tokens = {sanitize_module_name(compat): compat for compat in compatibles}

    for token, compat in compat_tokens.items():
        for name in binding_modules.get(token, []):
            _add_req(reqs, name, f'binding:{compat}')

    enabled_symbols = set(collect_conf_symbols(app_dir)) if app_dir else set()

    for dep in kconfig_deps:
        triggered = False
        source = None
        if dep.dt_tokens:
            for token in dep.dt_tokens:
                if token in compat_tokens:
                    triggered = True
                    source = f'driver:{dep.symbol} ({compat_tokens[token]})'
                    break
        elif dep.symbol in enabled_symbols:
            triggered = True
            source = f'kconfig:{dep.symbol}'
        if triggered:
            for name in dep.modules:
                _add_req(reqs, name, source)

    return ResolvedModules(required=list(reqs.values()))


def list_declared_modules(board_roots: list[Path] | None = None,
                          soc_roots: list[Path] | None = None,
                          zephyr_base: Path | None = None,
                          kconfig_roots: list[Path] | None = None,
                          binding_roots: list[Path] | None = None) -> ResolvedModules:
    """Collect every module declared by YAML or Kconfig (no defaults)."""
    zephyr_base = Path(zephyr_base) if zephyr_base else Path(__file__).resolve().parent.parent
    board_roots = list(board_roots or [zephyr_base])
    soc_roots = list(soc_roots or [zephyr_base])
    project_names = load_manifest_project_names(zephyr_base)

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

    k_roots = list(kconfig_roots) if kconfig_roots is not None else [
        zephyr_base / 'drivers',
        zephyr_base / 'subsys',
        zephyr_base / 'lib',
        zephyr_base / 'modules',
    ]
    for dep in scan_kconfig_module_deps(k_roots, project_names):
        rel = dep.path
        for name in dep.modules:
            _add_req(reqs, name, f'kconfig:{dep.symbol} ({rel})')

    b_roots = list(binding_roots) if binding_roots is not None else [
        zephyr_base / 'dts' / 'bindings',
    ]
    for token, modules in scan_binding_modules(b_roots).items():
        for name in modules:
            _add_req(reqs, name, f'binding:{token}')

    return ResolvedModules(required=list(reqs.values()))


def add_args(parser):
    parser.add_argument('--board', default=None,
                        help='board name or board target used as the query')
    parser.add_argument('--shield', dest='shields', action='append', default=[],
                        help='shield name, may be given more than once')
    parser.add_argument('--app', dest='app_dir', type=Path, default=None,
                        help='application directory (DTS overlays, prj.conf, tests.yaml)')
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
                        help='list every module declared in YAML or Kconfig')
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
                app_dir=args.app_dir,
            )
    except RuntimeError as e:
        print(f'ERROR: {e}', file=sys.stderr)
        sys.exit(1)

    dump_modules(resolved, args)


if __name__ == '__main__':
    main()
