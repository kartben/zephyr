#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""
Describe the base devicetree of a board target as JSON.

The board's base devicetree - the one a build without application overlays,
shields or snippets uses - is produced by the Zephyr CMake package itself.
cmake/package_helper.cmake runs the 'dts' module the way a build does and
returns as soon as the devicetree is available, so board aliases, deprecated
board names, revisions, qualifiers, module roots, devicetree file discovery
and the board's own pre_dt_board.cmake are all handled by the build system
rather than reimplemented here. No Kconfig is run and nothing is compiled.

That run leaves behind the same 'zephyr.dts' and 'edt.pickle' a build would
produce, plus a 'build_info.yml' naming the board target and the devicetree
inputs. This module turns those into one JSON object per board target: the
resolved target, the devicetree input files, the chosen and aliases entries,
the node labels, the enabled compatibles and every node with its registers,
interrupts and property values.

This module is shared between the 'west boards --describe' command and
standalone use (python3 scripts/board_description.py --target <target>).
"""

import argparse
import json
import pickle
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import yaml

ZEPHYR_BASE = Path(__file__).resolve().parents[1]
PACKAGE_HELPER = ZEPHYR_BASE / 'cmake' / 'package_helper.cmake'

sys.path.insert(0, str(ZEPHYR_BASE / 'scripts'))
# edtlib must be importable to load the edt.pickle written by the CMake run.
sys.path.insert(0, str(ZEPHYR_BASE / 'scripts' / 'dts' / 'python-devicetree' / 'src'))

import list_boards  # noqa: E402
from devicetree import edtlib  # noqa: E402

# Roots forwarded to CMake, which merges them with the ones the Zephyr
# modules of the workspace contribute.
ROOT_ARGS = ('BOARD_ROOT', 'SOC_ROOT', 'ARCH_ROOT', 'DTS_ROOT')


class BoardDescriptionError(RuntimeError):
    pass


class BoardDescriber:
    """Describes the devicetree of the board targets of one workspace."""

    def __init__(
        self,
        boards: dict,
        roots: dict[str, list] | None = None,
        workspace_dir: Path | None = None,
        preprocessor: str | None = None,
    ):
        self.boards = boards
        self.workspace_dir = Path(workspace_dir or ZEPHYR_BASE.parent).resolve()
        self.cmake = shutil.which('cmake')
        if self.cmake is None:
            raise BoardDescriptionError('cmake not found in PATH')

        # The CMake run writes the edt.pickle read back here, so both sides
        # have to be the same interpreter.
        self.cmake_args = [f'-DPython3_EXECUTABLE={sys.executable}']
        if preprocessor:
            self.cmake_args.append(f'-DCMAKE_DTS_PREPROCESSOR={preprocessor}')
        for name, paths in (roots or {}).items():
            if name not in ROOT_ARGS:
                raise BoardDescriptionError(f'Unknown root {name}')
            if paths:
                self.cmake_args.append(f'-D{name}=' + ';'.join(str(p) for p in paths))

    def all_targets(self, boards=None) -> list[str]:
        """All '<board>/<qualifiers>' targets, at the boards' default revisions."""
        return [
            f'{board.name}/{qualifier}'
            for board in (boards or self.boards).values()
            for qualifier in list_boards.board_v2_qualifiers(board)
        ]

    def describe(self, target: str, output_dir: Path | None = None) -> dict:
        """Return the description of one board target.

        The merged devicetree is written to 'output_dir' when given, named
        after the resolved target.
        """
        with tempfile.TemporaryDirectory(prefix='board_description_') as tmp:
            build_dir = self._run_cmake(target, Path(tmp))
            try:
                build_info = self._read_build_info(build_dir)
                edt = self._read_edt(build_dir)
            except (OSError, KeyError) as e:
                raise BoardDescriptionError(f'incomplete devicetree generation: {e}') from e

            description = {
                'board': self._board_description(target, build_info['board']),
                'devicetree': self._devicetree_description(build_info['devicetree']),
                'chosen': {name: node.path for name, node in edt.chosen_nodes.items()},
                'aliases': {alias: node.path for node in edt.nodes for alias in node.aliases},
                'labels': {label: node.path for label, node in edt.label2node.items()},
                'compatibles': {
                    'okay': sorted(edt.compat2okay),
                    'all': sorted(edt.compat2nodes),
                },
                'nodes': [
                    self._node_description(node)
                    for node in sorted(edt.nodes, key=lambda n: n.dep_ordinal)
                ],
            }

            if output_dir is not None:
                shutil.copyfile(
                    build_dir / 'zephyr' / 'zephyr.dts',
                    output_dir / f'{file_stem(description)}.dts',
                )

        return description

    def _run_cmake(self, target: str, tmp: Path) -> Path:
        """Run the 'dts' module of the Zephyr CMake package for one target."""
        # An empty application, so that nothing application specific - an
        # app.overlay in particular - ends up in the devicetree. The
        # (equally empty) prj.conf is what makes it a Zephyr application.
        app_dir = tmp / 'app'
        app_dir.mkdir()
        (app_dir / 'prj.conf').touch()
        build_dir = tmp / 'build'

        cmd = [
            self.cmake,
            f'-DBOARD={target}',
            '-B',
            str(build_dir),
            '-S',
            str(app_dir),
            '-DMODULES=dts',
            *self.cmake_args,
            '-P',
            str(PACKAGE_HELPER),
        ]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            raise BoardDescriptionError(_cmake_error(result))
        return build_dir

    @staticmethod
    def _read_build_info(build_dir: Path) -> dict:
        with (build_dir / 'build_info.yml').open(encoding='utf-8') as f:
            return yaml.safe_load(f)['cmake']

    @staticmethod
    def _read_edt(build_dir: Path) -> edtlib.EDT:
        with (build_dir / 'zephyr' / 'edt.pickle').open('rb') as f:
            return pickle.load(f)

    def _board_description(self, requested: str, info: dict) -> dict:
        name = info['name']
        qualifiers = info.get('qualifiers') or ''
        revision = info.get('revision') or None
        board = self.boards.get(name)
        if board is None:
            raise BoardDescriptionError(
                f"resolved to board '{name}', which is not in the board list"
            )
        socs = [soc.name for soc in board.socs]
        first = qualifiers.split('/')[0] if qualifiers else None

        target = name
        if revision:
            target += f'@{revision}'
        if qualifiers:
            target += f'/{qualifiers}'

        _, _, requested_revision = requested.partition('@')

        return {
            'name': name,
            'full_name': board.full_name,
            'vendor': board.vendor,
            'hwm': board.hwm,
            'target': target,
            'requested_target': requested,
            'normalized_target': f'{name}/{qualifiers}'.rstrip('/').replace('/', '_'),
            'qualifiers': qualifiers,
            'soc': first if first in socs else None,
            'revision': {
                'format': board.revision_format,
                'default': board.revision_default,
                'requested': requested_revision.split('/')[0] or None,
                'active': revision,
            },
            'directories': [self._rel(d) for d in info.get('path', [])],
        }

    def _devicetree_description(self, info: dict) -> dict:
        files = info.get('files', [])
        bindings_dirs = info.get('bindings-dirs', [])
        vendor_prefixes = [
            f'{d}/vendor-prefixes.txt'
            for d in bindings_dirs
            if (Path(d) / 'vendor-prefixes.txt').is_file()
        ]
        return {
            # dts.cmake puts the board .dts first and the revision
            # overlays applied on top of it after it.
            'source': self._rel(files[0]) if files else None,
            'overlays': [self._rel(f) for f in files[1:]],
            'files': [self._rel(f) for f in info.get('include-files', [])],
            'include_dirs': [self._rel(d) for d in info.get('include-dirs', [])],
            'bindings_dirs': [self._rel(d) for d in bindings_dirs],
            'vendor_prefixes': [self._rel(f) for f in vendor_prefixes],
            'extra_dtc_flags': info.get('extra-dtc-flags', []),
        }

    def _node_description(self, node: edtlib.Node) -> dict:
        return {
            'path': node.path,
            'name': node.name,
            'unit_addr': node.unit_addr,
            'labels': node.labels,
            'aliases': node.aliases,
            'status': node.status,
            'compats': node.compats,
            'matching_compat': node.matching_compat,
            'binding': self._rel(node.binding_path) if node.binding_path else None,
            'parent': node.parent.path if node.parent else None,
            'on_bus': node.on_bus,
            'buses': node.buses,
            'dep_ordinal': node.dep_ordinal,
            'regs': [{'name': r.name, 'addr': r.addr, 'size': r.size} for r in node.regs],
            'interrupts': [self._controller_and_data(i) for i in node.interrupts],
            'props': {name: self._prop_value(prop.val) for name, prop in node.props.items()},
        }

    @classmethod
    def _controller_and_data(cls, cad: edtlib.ControllerAndData) -> dict:
        return {
            'name': cad.name,
            'controller': cad.controller.path,
            'data': {k: cls._prop_value(v) for k, v in cad.data.items()},
        }

    @classmethod
    def _prop_value(cls, val):
        if isinstance(val, edtlib.Node):
            return val.path
        if isinstance(val, edtlib.ControllerAndData):
            return cls._controller_and_data(val)
        if isinstance(val, bytes):
            return list(val)
        if isinstance(val, list):
            return [cls._prop_value(v) for v in val]
        return val

    def _rel(self, path) -> str:
        path = Path(path)
        try:
            return path.resolve().relative_to(self.workspace_dir).as_posix()
        except ValueError:
            return path.as_posix()


def _cmake_error(result: subprocess.CompletedProcess, lines: int = 20) -> str:
    """A short error message from a failed CMake run.

    CMake splits its output over both streams - the board suggestions of an
    unknown board target go to stdout, the fatal error itself to stderr - so
    the tail of both is reported.
    """
    output = '\n'.join(s for s in (result.stdout, result.stderr) if s).strip().splitlines()
    reason = '\n'.join(output[-lines:]) if output else 'no output'
    return f'devicetree generation failed (error code {result.returncode}):\n{reason}'


def file_stem(description: dict) -> str:
    """File name stem for one target, named like the board's own files."""
    board = description['board']
    revision = (board['revision']['active'] or '').replace('.', '_')
    return '_'.join(p for p in (board['normalized_target'], revision) if p)


def describe_all(describer: BoardDescriber, targets, output_dir, out=sys.stdout, err=print):
    """Describe 'targets'; returns the number of failures.

    Descriptions go to one file per target in 'output_dir' when given, and
    are otherwise printed to 'out' as one JSON object keyed by target.
    """
    if output_dir is not None:
        output_dir.mkdir(parents=True, exist_ok=True)

    descriptions = {}
    failures = 0
    for target in targets:
        try:
            description = describer.describe(target, output_dir)
        except BoardDescriptionError as e:
            err(f'{target}: {e}')
            failures += 1
            continue

        resolved = description['board']['target']
        if output_dir is None:
            descriptions[resolved] = description
        else:
            out.write(f'{resolved}: {_write_json(description, output_dir)}\n')

    if output_dir is None:
        json.dump(descriptions, out, indent=2)
        out.write('\n')
    return failures


def _write_json(description: dict, output_dir: Path) -> Path:
    json_out = output_dir / f'{file_stem(description)}.json'
    with json_out.open('w', encoding='utf-8') as f:
        json.dump(description, f, indent=2)
        f.write('\n')
    return json_out


def add_args(parser):
    # Remember to update west-completion.bash if you add or remove
    # flags
    parser.add_argument(
        '-t',
        '--target',
        dest='targets',
        default=[],
        action='append',
        help='''board target (<board>[@<revision>][/<qualifiers>]) to
                describe, may be given more than once; without it every
                target of every listed board is described, at each board's
                default revision''',
    )
    parser.add_argument(
        '-o',
        '--output-dir',
        type=Path,
        help='''write one <board target>.json and .dts file per target here
                instead of printing the descriptions to stdout''',
    )
    parser.add_argument(
        '--preprocessor',
        help='''C preprocessor to run on the devicetree files (default: the
                first C compiler CMake finds)''',
    )


def parse_args():
    parser = argparse.ArgumentParser(allow_abbrev=False, description=__doc__)
    list_boards.add_args(parser)
    add_args(parser)
    parser.add_argument(
        '--dts-root',
        dest='dts_roots',
        default=[],
        type=Path,
        action='append',
        help='add a devicetree root, may be given more than once',
    )
    parser.add_argument(
        '--workspace-dir',
        type=Path,
        help='directory used as reference for relative paths (e.g. WEST_TOPDIR)',
    )
    return parser.parse_args()


def main():
    args = parse_args()

    # CMake knows about ZEPHYR_BASE and the module roots already, so only
    # the roots given on the command line have to be passed on to it.
    roots = {
        'ARCH_ROOT': list(args.arch_roots),
        'BOARD_ROOT': list(args.board_roots),
        'SOC_ROOT': list(args.soc_roots),
        'DTS_ROOT': list(args.dts_roots),
    }
    args.arch_roots.append(ZEPHYR_BASE)
    args.board_roots.append(ZEPHYR_BASE)
    args.soc_roots.append(ZEPHYR_BASE)

    describer = BoardDescriber(
        list_boards.find_v2_boards(args),
        roots=roots,
        workspace_dir=args.workspace_dir,
        preprocessor=args.preprocessor,
    )
    targets = args.targets or describer.all_targets()
    failures = describe_all(
        describer, targets, args.output_dir, err=lambda m: print(f'ERROR: {m}', file=sys.stderr)
    )
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    try:
        main()
    except BoardDescriptionError as e:
        print(f'ERROR: {e}', file=sys.stderr)
        sys.exit(1)
