# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

'''Read-only views of the west workspace: projects, modules, boards.'''

import os
import re
import shutil
import sys
from argparse import Namespace
from pathlib import Path

from west.version import __version__ as west_version

from boards import board_to_dict
from zephyr_ext_common import module_roots

sys.path.append(os.fspath(Path(__file__).parent.parent.parent))
import list_boards as list_boards_lib
import zephyr_module

HOST_TOOLS = ('cmake', 'ninja', 'dtc', 'gperf', 'git')

CONFIG_KEYS = (
    'build.board',
    'build.dir-fmt',
    'build.guess-dir',
    'build.pristine',
    'build.sysbuild',
    'build.cmake-args',
)


def zephyr_version(zephyr_base) -> str | None:
    values = {}
    try:
        for line in (Path(zephyr_base) / 'VERSION').read_text().splitlines():
            key, _, value = line.partition('=')
            values[key.strip()] = value.strip()
    except OSError:
        return None
    version = '.'.join(values.get(k, '0') for k in ('VERSION_MAJOR', 'VERSION_MINOR', 'PATCHLEVEL'))
    if values.get('EXTRAVERSION'):
        version += f'-{values["EXTRAVERSION"]}'
    return version


def sdk_registry() -> list[str]:
    # SDK installations registered with CMake, the usual way builds find one.
    registry = Path.home() / '.cmake' / 'packages' / 'Zephyr-sdk'
    sdks = []
    if registry.is_dir():
        for entry in sorted(registry.iterdir()):
            try:
                sdks.append(str(Path(entry.read_text().strip()).parent))
            except OSError:
                continue
    return sdks


def environment_check(cfg) -> dict:
    '''Report whether the environment the server inherited can build.'''
    tools = {name: shutil.which(name) for name in HOST_TOOLS}
    missing = [name for name, path in tools.items() if path is None]
    variant = os.environ.get('ZEPHYR_TOOLCHAIN_VARIANT')
    sdk_dir = os.environ.get('ZEPHYR_SDK_INSTALL_DIR')
    sdks = sdk_registry()
    hints = []
    if missing:
        hints.append(
            f'{", ".join(missing)} not found on PATH; builds will fail. Add the '
            'directories to the PATH in the MCP client configuration, or start '
            'the server from a launcher script that sets up the environment.'
        )
    if not (sdks or sdk_dir or variant):
        hints.append(
            'no Zephyr SDK is registered with CMake and no toolchain variables are '
            'set; run "west sdk install", or pass ZEPHYR_TOOLCHAIN_VARIANT and '
            'friends in the MCP client configuration.'
        )
    return {
        'python': sys.executable,
        'path': os.environ.get('PATH', ''),
        'tools': tools,
        'missing_tools': missing,
        'toolchain_variant': variant,
        'sdk_install_dir': sdk_dir,
        'sdk_registry': sdks,
        'hints': hints,
    }


def workspace_info(cfg) -> dict:
    projects = cfg.manifest.projects
    return {
        'topdir': cfg.topdir,
        'zephyr_base': cfg.zephyr_base,
        'manifest_path': cfg.manifest.path,
        'west_version': west_version,
        'zephyr_version': zephyr_version(cfg.zephyr_base),
        'config': {key: cfg.config.get(key) for key in CONFIG_KEYS},
        'projects_total': len(projects),
        'projects_cloned': sum(1 for p in projects if p.is_cloned()),
        'allowed_roots': cfg.roots,
        'allow_hardware': cfg.allow_hardware,
        'log_dir': cfg.log_dir,
        'environment': environment_check(cfg),
    }


def list_projects(cfg, cloned_only=False, with_sha=False) -> dict:
    projects = []
    for project in cfg.manifest.projects:
        cloned = project.is_cloned()
        if cloned_only and not cloned:
            continue
        entry = {
            'name': project.name,
            'path': project.path,
            'abspath': project.abspath,
            'url': project.url,
            'revision': project.revision,
            'cloned': cloned,
        }
        if with_sha:
            entry['sha'] = project.sha('HEAD') if cloned else None
        projects.append(entry)
    return {'projects': projects}


def list_modules(cfg) -> dict:
    modules = []
    for module in zephyr_module.parse_modules(cfg.zephyr_base, cfg.manifest):
        build = module.meta.get('build', {})
        modules.append(
            {
                'name': module.meta.get('name'),
                'path': module.project,
                'cmake': build.get('cmake'),
                'kconfig': build.get('kconfig'),
                'settings': build.get('settings', {}),
                'depends': list(module.depends),
                'blobs': len(module.meta.get('blobs') or []),
            }
        )
    return {'modules': modules}


def _board_args(cfg, board=None) -> Namespace:
    roots = module_roots(cfg.manifest, ['arch_root', 'board_root', 'soc_root'])
    return Namespace(
        arch_roots=roots['arch_root'],
        board_roots=roots['board_root'],
        soc_roots=roots['soc_root'],
        board=board,
        board_dir=[],
    )


def list_boards(cfg, name_re=None, vendor=None, limit=200) -> dict:
    pattern = re.compile(name_re) if name_re else None
    boards = [
        b
        for b in list_boards_lib.find_v2_boards(_board_args(cfg)).values()
        if (pattern is None or pattern.search(b.name)) and (vendor is None or b.vendor == vendor)
    ]
    boards.sort(key=lambda b: b.name)
    entries = []
    for board in boards[:limit]:
        entry = board_to_dict(board)
        del entry['targets']
        entries.append(entry)
    return {'count': len(boards), 'truncated': len(boards) > limit, 'boards': entries}


def board_info(cfg, board) -> dict:
    found = list_boards_lib.find_v2_boards(_board_args(cfg, board))
    if board not in found:
        raise ValueError(f'unknown board {board}; use list_boards to search for it')
    info = board_to_dict(found[board])
    doc_index = found[board].dir / 'doc' / 'index.rst'
    info['board_yml'] = found[board].dir / 'board.yml'
    info['doc_index'] = doc_index if doc_index.is_file() else None
    return info
