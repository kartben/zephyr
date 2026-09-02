# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

'''Read-only views of a Zephyr build directory.'''

import re
from argparse import Namespace
from pathlib import Path

import yaml

from build_helpers import is_zephyr_build, load_domains
from run_common import build_runner_context, get_build_dir, is_sysbuild
from runners.core import BuildConfiguration
from zcmake import CMakeCache
from zephyr_mcp.paths import resolve_under_roots

CACHE_KEYS = (
    'CACHED_BOARD',
    'BOARD_DIR',
    'ZEPHYR_BASE',
    'APPLICATION_SOURCE_DIR',
    'APPLICATION_CONFIG_DIR',
    'CMAKE_BUILD_TYPE',
    'ZEPHYR_TOOLCHAIN_VARIANT',
    'SNIPPET',
    'SHIELD',
)

SCALAR_PROP_TYPES = ('int', 'string', 'boolean', 'array', 'string-array', 'uint8-array')


def resolve_build_dir(cfg, build_dir=None) -> Path:
    '''Locate and validate the build directory a tool should look at.'''
    if build_dir is None:
        build_dir = get_build_dir(Namespace(build_dir=None), die_if_none=False, config=cfg.config)
        if build_dir is None:
            raise ValueError(
                'no build_dir given and no default build directory found; pass build_dir explicitly'
            )
    path = resolve_under_roots(build_dir, cfg.roots, cwd=cfg.topdir)
    if not is_zephyr_build(str(path)):
        raise ValueError(f'{path} is not a Zephyr build directory')
    return path


def domain_build_dir(build_dir: Path, domain=None) -> Path:
    '''Return the (domain) build directory holding zephyr/.config etc.'''
    if not is_sysbuild(str(build_dir)):
        if domain:
            raise ValueError(f'{build_dir} is not a sysbuild build directory; drop domain')
        return build_dir
    domains = load_domains(str(build_dir))
    if domain is None:
        return Path(domains.get_default_domain().build_dir)
    names = [d.name for d in domains.get_domains()]
    if domain not in names:
        raise ValueError(f'unknown domain {domain}; domains: {", ".join(names)}')
    return Path(domains.get_domain(domain).build_dir)


def build_dir_info(cfg, build_dir=None) -> dict:
    top = resolve_build_dir(cfg, build_dir)
    sysbuild = is_sysbuild(str(top))
    domains = []
    if sysbuild:
        loaded = load_domains(str(top))
        domains = [
            {
                'name': d.name,
                'build_dir': d.build_dir,
                'default': d.name == loaded.get_default_domain().name,
            }
            for d in loaded.get_domains()
        ]
    app = domain_build_dir(top)
    cache = CMakeCache.from_build_dir(str(app))
    build_info = None
    build_info_path = top / 'build_info.yml'
    if build_info_path.is_file():
        build_info = yaml.safe_load(build_info_path.read_text())
    return {
        'build_dir': top,
        'sysbuild': sysbuild,
        'domains': domains,
        'board': cache.get('CACHED_BOARD'),
        'cache': {key: cache.get(key) for key in CACHE_KEYS if key in cache},
        'build_info': build_info,
        'artifacts': {
            name: str(app / 'zephyr' / name)
            for name in (
                'zephyr.elf',
                'zephyr.hex',
                'zephyr.bin',
                'zephyr.map',
                'zephyr.dts',
                '.config',
                'edt.pickle',
                'runners.yaml',
            )
            if (app / 'zephyr' / name).is_file()
        },
    }


def kconfig(cfg, build_dir=None, symbols=None, pattern=None, domain=None) -> dict:
    app = domain_build_dir(resolve_build_dir(cfg, build_dir), domain)
    dotconfig = app / 'zephyr' / '.config'
    values = {}
    for line in dotconfig.read_text().splitlines():
        if m := re.match(r'^(CONFIG_\w+)=(.*)$', line):
            value = m.group(2)
            if value.startswith('"') and value.endswith('"'):
                value = value[1:-1]
            values[m.group(1)] = value
        elif m := re.match(r'^# (CONFIG_\w+) is not set$', line):
            values[m.group(1)] = 'n'

    if symbols is None and pattern is None:
        raise ValueError('give symbols (a list of CONFIG_ names) or pattern (a regex)')
    wanted = {}
    missing = []
    for symbol in symbols or []:
        name = symbol if symbol.startswith('CONFIG_') else f'CONFIG_{symbol}'
        if name in values:
            wanted[name] = values[name]
        else:
            missing.append(name)
    if pattern:
        regex = re.compile(pattern)
        wanted.update({k: v for k, v in values.items() if regex.search(k)})
    return {'build_dir': app, 'config_file': dotconfig, 'values': wanted, 'missing': missing}


def runners_info(cfg, build_dir=None, domain=None) -> dict:
    top = resolve_build_dir(cfg, build_dir)
    return build_runner_context(top, [domain] if domain else None)


def _prop_value(prop):
    if prop.type in SCALAR_PROP_TYPES:
        return list(prop.val) if isinstance(prop.val, bytes | tuple) else prop.val
    if prop.type in ('phandle', 'path'):
        return prop.val.path
    if prop.type == 'phandles':
        return [node.path for node in prop.val]
    if prop.type == 'phandle-array':
        return [
            None if entry is None else {'controller': entry.controller.path, **entry.data}
            for entry in prop.val
        ]
    return None


def node_to_dict(node) -> dict:
    return {
        'path': node.path,
        'name': node.name,
        'labels': list(node.labels),
        'compats': list(node.compats),
        'status': node.status,
        'unit_addr': node.unit_addr,
        'regs': [{'name': reg.name, 'addr': reg.addr, 'size': reg.size} for reg in node.regs],
        'parent': node.parent.path if node.parent else None,
        'on_bus': node.on_bus,
        'props': {
            name: _prop_value(prop)
            for name, prop in node.props.items()
            if _prop_value(prop) is not None
        },
    }


def devicetree_query(
    cfg,
    build_dir=None,
    compatible=None,
    label=None,
    chosen=None,
    path=None,
    status='okay',
    domain=None,
) -> dict:
    app = domain_build_dir(resolve_build_dir(cfg, build_dir), domain)
    selectors = [s for s in (compatible, label, chosen, path) if s is not None]
    if len(selectors) != 1:
        raise ValueError('give exactly one of compatible, label, chosen or path')
    if not (app / 'zephyr' / 'edt.pickle').is_file():
        raise ValueError(f'{app} has no zephyr/edt.pickle; build the application first')
    edt = BuildConfiguration(str(app)).edt

    if compatible is not None:
        nodes = list(edt.compat2nodes.get(compatible, []))
    elif label is not None:
        node = edt.label2node.get(label)
        nodes = [node] if node else []
    elif chosen is not None:
        node = edt.chosen_nodes.get(chosen)
        nodes = [node] if node else []
    else:
        nodes = [n for n in edt.nodes if n.path == path]

    if status == 'okay':
        nodes = [n for n in nodes if n.status == 'okay']
    return {'build_dir': app, 'count': len(nodes), 'nodes': [node_to_dict(n) for n in nodes]}
