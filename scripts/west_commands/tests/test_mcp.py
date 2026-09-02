# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

import asyncio
import json
import os
import pickle
import sys
from collections import namedtuple
from pathlib import Path
from unittest.mock import patch

import pytest
from conftest import _FakeConfig

from mcp_cmd import Mcp
from zephyr_ext_common import ZEPHYR_BASE
from zephyr_mcp import build as build_mod
from zephyr_mcp import builddir, twister, workspace
from zephyr_mcp.config import ServerConfig
from zephyr_mcp.paths import resolve_under_roots
from zephyr_mcp.proc import ANSI_RE, run_west, run_west_capture, west_argv

pytestmark = pytest.mark.skipif(os.name == 'nt', reason='POSIX paths and processes')


class FakeProject:
    def __init__(self, name, path, cloned=True):
        self.name = name
        self.path = path
        self.abspath = f'/ws/{path}'
        self.url = f'https://example.com/{name}'
        self.revision = 'main'
        self._cloned = cloned

    def is_cloned(self):
        return self._cloned

    def sha(self, rev):
        return 'abc123'


class FakeManifest:
    path = '/ws/zephyr/west.yml'
    projects = [FakeProject('manifest', 'zephyr'), FakeProject('hal', 'modules/hal', False)]


@pytest.fixture
def cfg(tmp_path):
    topdir = tmp_path / 'ws'
    topdir.mkdir()
    return ServerConfig(
        topdir=topdir,
        roots=[topdir, tmp_path / 'extra'],
        log_dir=tmp_path / 'logs',
        manifest=FakeManifest(),
        config=_FakeConfig({'build.board': 'native_sim'}),
    )


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return path


DTS = '''\
/dts-v1/;
/ {
    #address-cells = <1>;
    #size-cells = <1>;
    chosen {
        zephyr,console = &uart0;
    };
    soc {
        #address-cells = <1>;
        #size-cells = <1>;
        uart0: uart@40001000 {
            compatible = "vnd,uart";
            reg = <0x40001000 0x1000>;
            status = "okay";
            current-speed = <115200>;
            label = "UART_0";
        };
        uart1: uart@40002000 {
            compatible = "vnd,uart";
            reg = <0x40002000 0x1000>;
            status = "disabled";
            current-speed = <9600>;
        };
    };
};
'''

BINDING = '''\
description: Test UART
compatible: "vnd,uart"
properties:
  reg:
    type: array
  current-speed:
    type: int
  label:
    type: string
'''


def make_build_dir(root, board='native_sim', with_edt=False, sysbuild_domains=None):
    write(
        root / 'CMakeCache.txt',
        f'ZEPHYR_BASE:PATH={ZEPHYR_BASE}\nCACHED_BOARD:STRING={board}\n'
        f'APPLICATION_SOURCE_DIR:PATH=/ws/app\n',
    )
    write(
        root / 'zephyr' / '.config',
        f'CONFIG_BOARD_TARGET="{board}"\nCONFIG_GPIO=y\n# CONFIG_SPI is not set\n'
        'CONFIG_MAIN_STACK_SIZE=2048\nCONFIG_LOG_DEFAULT_LEVEL=3\n',
    )
    write(
        root / 'zephyr' / 'runners.yaml',
        'runners:\n- native\nflash-runner: native\ndebug-runner: native\n'
        'config:\n  board_dir: /z/boards/native/native_sim\n  elf_file: zephyr.exe\n'
        'args:\n  native: []\n',
    )
    write(root / 'zephyr' / 'zephyr.elf', '')
    write(root / 'build_info.yml', 'version: 0.1.0\ncmake:\n  board:\n    name: native_sim\n')
    if with_edt:
        sys.path.insert(0, str(ZEPHYR_BASE / 'scripts' / 'dts' / 'python-devicetree' / 'src'))
        from devicetree import edtlib

        dts = write(root / 'zephyr' / 'zephyr.dts', DTS)
        bindings = root / 'bindings'
        write(bindings / 'vnd,uart.yaml', BINDING)
        edt = edtlib.EDT(str(dts), [str(bindings)])
        with open(root / 'zephyr' / 'edt.pickle', 'wb') as f:
            pickle.dump(edt, f, protocol=4)
    if sysbuild_domains:
        domains = ''.join(f'  - name: {d}\n    build_dir: {root / d}\n' for d in sysbuild_domains)
        write(
            root / 'domains.yaml',
            f'default: {sysbuild_domains[0]}\nbuild_dir: {root}\ndomains:\n{domains}',
        )
    return root


def test_config_roots_always_include_topdir(tmp_path):
    topdir = tmp_path / 'ws'
    topdir.mkdir()
    cfg = ServerConfig(topdir=topdir, roots=[tmp_path / 'app', topdir], log_dir=tmp_path / 'l')
    assert cfg.roots == [topdir.resolve(), (tmp_path / 'app').resolve()]
    assert ServerConfig(topdir=topdir, log_dir=tmp_path / 'l').roots == [topdir.resolve()]


# --- paths ---------------------------------------------------------------


def test_resolve_under_roots(tmp_path):
    root = tmp_path / 'root'
    (root / 'sub').mkdir(parents=True)
    outside = tmp_path / 'outside'
    outside.mkdir()
    assert resolve_under_roots('sub', [root]) == root / 'sub'
    assert resolve_under_roots(root / 'sub' / 'x', [root]) == root / 'sub' / 'x'
    assert resolve_under_roots('.', [root]) == root
    with pytest.raises(ValueError, match='outside'):
        resolve_under_roots('../outside', [root])
    with pytest.raises(ValueError):
        resolve_under_roots(outside, [root])
    (root / 'link').symlink_to(outside)
    with pytest.raises(ValueError):
        resolve_under_roots('link/file', [root])
    # A second root makes it legal.
    assert resolve_under_roots('link/file', [root, outside]) == outside / 'file'


# --- proc ----------------------------------------------------------------


def test_west_argv():
    assert west_argv('build', '-b', Path('x')) == [sys.executable, '-m', 'west', 'build', '-b', 'x']


def test_run_west_streams_lines_and_logs(cfg):
    seen = []

    async def on_line(line):
        seen.append(line)

    script = "import sys; sys.stdout.write('a\\rb\\n'); print('\\x1b[92mgreen\\x1b[0m'); print()"
    res = asyncio.run(
        run_west(cfg, [sys.executable, '-c', script], log_name='t', timeout_s=30, on_line=on_line)
    )
    assert res.returncode == 0 and not res.timed_out
    assert seen == ['a', 'b', 'green']
    assert res.tail == seen
    assert Path(res.log_path).read_bytes() == b'a\rb\n\x1b[92mgreen\x1b[0m\n\n'
    assert Path(res.log_path).parent == cfg.log_dir


def test_run_west_timeout_kills(cfg):
    script = 'import time; print("started", flush=True); time.sleep(30)'
    res = asyncio.run(run_west(cfg, [sys.executable, '-c', script], log_name='t', timeout_s=1))
    assert res.timed_out and res.returncode != 0
    assert res.tail == ['started']


def test_run_west_capture(cfg):
    script = 'import sys; print("{\\"a\\": 1}"); print("noise", file=sys.stderr)'
    res = asyncio.run(
        run_west_capture(cfg, [sys.executable, '-c', script], log_name='c', timeout_s=30)
    )
    assert json.loads(res.output) == {'a': 1}
    assert res.tail == ['noise']


# --- regexes and argv builders ------------------------------------------


def test_twister_regexes():
    line = (
        'INFO    - Total complete: \x1b[92m  12/  40\x1b[0m  30%  built (not run): 3, '
        'filtered: 5, failed: 1, error: 2\r'
    )
    m = twister.TICKER_RE.search(ANSI_RE.sub('', line))
    assert [int(g) for g in m.groups()] == [12, 40, 30, 1, 2]
    m = twister.SELECTED_RE.search(
        'INFO    - 3 test scenarios (12 configurations) selected, 4 configurations filtered'
    )
    assert (m[1], m[2]) == ('3', '12')


def test_build_regexes():
    assert build_mod.NINJA_RE.match('[12/345] Building C object x.c.obj').groups() == ('12', '345')
    diags = build_mod.parse_diagnostics(
        [
            '/ws/app/src/main.c:12:5: error: unknown type name foo',
            '/ws/app/src/main.c:12:5: error: unknown type name foo',
            'src/other.c:3: warning: implicit declaration',
            'ninja: build stopped: subcommand failed.',
            '[1/2] Building',
        ]
    )
    assert diags == [
        {
            'file': '/ws/app/src/main.c',
            'line': 12,
            'col': 5,
            'severity': 'error',
            'message': 'unknown type name foo',
        },
        {
            'file': 'src/other.c',
            'line': 3,
            'col': None,
            'severity': 'warning',
            'message': 'implicit declaration',
        },
        {
            'file': None,
            'line': None,
            'col': None,
            'severity': 'error',
            'message': 'ninja: build stopped: subcommand failed.',
        },
    ]


def test_argv_builders():
    west = [sys.executable, '-m', 'west']
    assert build_mod.build_argv(
        '/ws/app',
        'native_sim',
        '/ws/build',
        'always',
        True,
        snippets=['rtt'],
        shields=['x'],
        extra_conf=['a.conf'],
        extra_dtc_overlay=['b.overlay'],
        cmake_args=['-DFOO=1'],
        target='menuconfig',
    ) == [
        *west,
        'build',
        '-s',
        '/ws/app',
        '-p',
        'always',
        '-b',
        'native_sim',
        '-d',
        '/ws/build',
        '--sysbuild',
        '-S',
        'rtt',
        '--shield',
        'x',
        '--extra-conf',
        'a.conf',
        '--extra-dtc-overlay',
        'b.overlay',
        '-t',
        'menuconfig',
        '--',
        '-DFOO=1',
    ]
    assert build_mod.build_argv('/ws/app', sysbuild=False) == [
        *west,
        'build',
        '-s',
        '/ws/app',
        '-p',
        'auto',
        '--no-sysbuild',
    ]
    assert build_mod.flash_argv('/ws/build', 'jlink', 'app', '123', False, ['--erase']) == [
        *west,
        'flash',
        '-d',
        '/ws/build',
        '--skip-rebuild',
        '--domain',
        'app',
        '-r',
        'jlink',
        '--dev-id',
        '123',
        '--erase',
    ]
    assert twister.twister_argv(
        '/ws/out', ['tests/a'], ['s1'], ['native_sim'], ['t'], True, True, 4, ['-v']
    ) == [
        *west,
        'twister',
        '-O',
        '/ws/out',
        '-c',
        '-T',
        'tests/a',
        '-s',
        's1',
        '-p',
        'native_sim',
        '--tag',
        't',
        '-b',
        '-j',
        '4',
        '-v',
    ]
    assert twister.list_tests_argv(['tests/a'], ['t']) == [
        *west,
        'twister',
        '--list-tests',
        '--json',
        '-T',
        'tests/a',
        '--tag',
        't',
    ]


def test_check_extra_args():
    twister.check_extra_args(['-v', '--jobs=2'], allow_hardware=False)
    with pytest.raises(ValueError, match='allow-hardware'):
        twister.check_extra_args(['--device-testing'], allow_hardware=False)
    with pytest.raises(ValueError):
        twister.check_extra_args(['--hardware-map=map.yml'], allow_hardware=False)
    twister.check_extra_args(['--device-testing'], allow_hardware=True)


# --- workspace -----------------------------------------------------------


def test_workspace_info_and_projects(cfg):
    info = workspace.workspace_info(cfg)
    assert info['topdir'] == cfg.topdir
    assert info['projects_total'] == 2 and info['projects_cloned'] == 1
    assert info['config']['build.board'] == 'native_sim'
    assert info['zephyr_version'] and info['west_version']
    assert info['allow_hardware'] is False

    projects = workspace.list_projects(cfg, with_sha=True)['projects']
    assert [p['name'] for p in projects] == ['manifest', 'hal']
    assert projects[0]['sha'] == 'abc123' and projects[1]['sha'] is None
    assert workspace.list_projects(cfg, cloned_only=True)['projects'][0]['name'] == 'manifest'


Module = namedtuple('Module', ['project', 'meta', 'depends'])


def test_list_modules(cfg):
    modules = [
        Module(
            '/ws/modules/hal',
            {
                'name': 'hal',
                'build': {'cmake': '.', 'kconfig': 'Kconfig', 'settings': {'board_root': '.'}},
                'blobs': [{}, {}],
            },
            ['x'],
        )
    ]
    with patch('zephyr_mcp.workspace.zephyr_module.parse_modules', return_value=modules):
        out = workspace.list_modules(cfg)['modules']
    assert out == [
        {
            'name': 'hal',
            'path': '/ws/modules/hal',
            'cmake': '.',
            'kconfig': 'Kconfig',
            'settings': {'board_root': '.'},
            'depends': ['x'],
            'blobs': 2,
        }
    ]


def test_list_boards_and_board_info(cfg, tmp_path):
    from list_boards import Board, Soc

    boards = {
        'b_two': Board('b_two', [tmp_path / 'b_two'], 'v2', vendor='acme', socs=[Soc('s')]),
        'a_one': Board('a_one', [tmp_path / 'a_one'], 'v2', vendor='vnd', socs=[Soc('s')]),
    }
    roots = {'arch_root': [], 'board_root': [], 'soc_root': []}
    with (
        patch('zephyr_mcp.workspace.module_roots', return_value=roots),
        patch('zephyr_mcp.workspace.list_boards_lib.find_v2_boards', return_value=boards),
    ):
        out = workspace.list_boards(cfg)
        assert [b['name'] for b in out['boards']] == ['a_one', 'b_two']
        assert 'targets' not in out['boards'][0]
        assert workspace.list_boards(cfg, vendor='acme')['boards'][0]['name'] == 'b_two'
        assert workspace.list_boards(cfg, name_re='^a', limit=0) == {
            'count': 1,
            'truncated': True,
            'boards': [],
        }
        info = workspace.board_info(cfg, 'a_one')
        assert info['targets'] == ['a_one/s']
        assert info['board_yml'] == tmp_path / 'a_one' / 'board.yml'
        assert info['doc_index'] is None
        with pytest.raises(ValueError, match='unknown board'):
            workspace.board_info(cfg, 'nope')


# --- build directory -----------------------------------------------------


def test_build_dir_info_and_kconfig(cfg):
    build_dir = make_build_dir(cfg.topdir / 'build')
    info = builddir.build_dir_info(cfg, 'build')
    assert info['build_dir'] == build_dir and info['sysbuild'] is False
    assert info['board'] == 'native_sim'
    assert info['cache']['CACHED_BOARD'] == 'native_sim'
    assert info['build_info']['cmake']['board']['name'] == 'native_sim'
    assert set(info['artifacts']) == {'zephyr.elf', '.config', 'runners.yaml'}

    values = builddir.kconfig(cfg, 'build', symbols=['GPIO', 'CONFIG_SPI', 'CONFIG_NOPE'])
    assert values['values'] == {'CONFIG_GPIO': 'y', 'CONFIG_SPI': 'n'}
    assert values['missing'] == ['CONFIG_NOPE']
    values = builddir.kconfig(cfg, 'build', pattern='^CONFIG_(MAIN|LOG)')
    assert values['values'] == {'CONFIG_MAIN_STACK_SIZE': '2048', 'CONFIG_LOG_DEFAULT_LEVEL': '3'}
    with pytest.raises(ValueError):
        builddir.kconfig(cfg, 'build')

    runners = builddir.runners_info(cfg, 'build')
    assert runners['domains'][0]['default'] == {'flash': 'native', 'debug': 'native'}


def test_build_dir_validation(cfg, tmp_path):
    with pytest.raises(ValueError, match='not a Zephyr build'):
        builddir.build_dir_info(cfg, str(cfg.topdir))
    outside = make_build_dir(tmp_path / 'outside')
    with pytest.raises(ValueError, match='outside'):
        builddir.build_dir_info(cfg, str(outside))
    with (
        pytest.raises(ValueError, match='no build_dir'),
        patch('zephyr_mcp.builddir.get_build_dir', return_value=None),
    ):
        builddir.build_dir_info(cfg)


def test_sysbuild_domains(cfg):
    top = cfg.topdir / 'build'
    make_build_dir(top / 'app', sysbuild_domains=None)
    make_build_dir(top / 'mcuboot', board='other')
    make_build_dir(top, sysbuild_domains=['app', 'mcuboot'])
    info = builddir.build_dir_info(cfg, 'build')
    assert info['sysbuild'] is True
    assert [(d['name'], d['default']) for d in info['domains']] == [
        ('app', True),
        ('mcuboot', False),
    ]
    assert builddir.kconfig(cfg, 'build', symbols=['BOARD_TARGET'], domain='mcuboot')['values'] == {
        'CONFIG_BOARD_TARGET': 'other'
    }
    with pytest.raises(ValueError, match='unknown domain'):
        builddir.kconfig(cfg, 'build', symbols=['X'], domain='nope')
    with pytest.raises(ValueError, match='not a sysbuild'):
        builddir.kconfig(cfg, 'build/app', symbols=['X'], domain='app')


def test_devicetree_query(cfg):
    make_build_dir(cfg.topdir / 'build', with_edt=True)
    out = builddir.devicetree_query(cfg, 'build', compatible='vnd,uart')
    assert out['count'] == 1
    node = out['nodes'][0]
    assert node['path'] == '/soc/uart@40001000'
    assert node['labels'] == ['uart0']
    assert node['status'] == 'okay'
    assert node['unit_addr'] == 0x40001000
    assert node['regs'] == [{'name': None, 'addr': 0x40001000, 'size': 0x1000}]
    assert node['props']['current-speed'] == 115200
    assert node['props']['label'] == 'UART_0'
    assert node['props']['reg'] == [0x40001000, 0x1000]
    assert node['parent'] == '/soc'

    out = builddir.devicetree_query(cfg, 'build', compatible='vnd,uart', status='any')
    assert [n['path'] for n in out['nodes']] == ['/soc/uart@40001000', '/soc/uart@40002000']
    assert builddir.devicetree_query(cfg, 'build', label='uart1', status='any')['count'] == 1
    assert builddir.devicetree_query(cfg, 'build', label='uart1')['count'] == 0
    assert builddir.devicetree_query(cfg, 'build', chosen='zephyr,console')['nodes'][0][
        'labels'
    ] == ['uart0']
    assert builddir.devicetree_query(cfg, 'build', path='/soc')['count'] == 1
    with pytest.raises(ValueError, match='exactly one'):
        builddir.devicetree_query(cfg, 'build')
    with pytest.raises(ValueError, match='exactly one'):
        builddir.devicetree_query(cfg, 'build', label='a', chosen='b')


# --- twister results -----------------------------------------------------


REPORT = {
    'environment': {
        'os': 'posix',
        'zephyr_version': 'v4.4.99',
        'toolchain': 'zephyr',
        'commit_date': 'x',
        'run_date': 'y',
        'options': {'jobs': 4},
    },
    'testsuites': [
        {
            'name': 'k.a',
            'arch': 'x86',
            'platform': 'native_sim',
            'path': 'tests/a',
            'runnable': True,
            'retries': 0,
            'build_time': '1.00',
            'status': 'passed',
            'execution_time': '0.50',
            'testcases': [{'identifier': 'k.a.one', 'status': 'passed', 'execution_time': '0.10'}],
        },
        {
            'name': 'k.b',
            'arch': 'x86',
            'platform': 'native_sim',
            'path': 'tests/b',
            'runnable': True,
            'retries': 1,
            'build_time': '1.00',
            'status': 'failed',
            'reason': 'Timeout',
            'log': 'x' * 5000,
            'execution_time': '9.00',
            'testcases': [{'identifier': 'k.b.one', 'status': 'failed', 'reason': 'boom'}],
        },
        {
            'name': 'k.c',
            'arch': 'arm',
            'platform': 'qemu_x',
            'path': 'tests/c',
            'runnable': False,
            'retries': 0,
            'build_time': '0.00',
            'status': 'filtered',
            'reason': 'Platform excluded',
        },
    ],
}


def test_twister_results(cfg):
    out = cfg.topdir / 'twister-out'
    write(out / 'twister.json', json.dumps(REPORT))
    res = twister.twister_results(cfg, 'twister-out')
    assert res['summary'] == {'passed': 1, 'failed': 1, 'filtered': 1, 'total': 3}
    assert res['count'] == 3 and 'options' not in res['environment']
    assert 'log' not in res['testsuites'][0]

    res = twister.twister_results(
        cfg, str(out / 'twister.json'), status=['failed'], include_log=True
    )
    assert [s['name'] for s in res['testsuites']] == ['k.b']
    assert len(res['testsuites'][0]['log']) == twister.MAX_LOG_CHARS
    assert res['testsuites'][0]['testcases'] == [
        {'identifier': 'k.b.one', 'status': 'failed', 'reason': 'boom'}
    ]
    res = twister.twister_results(cfg, 'twister-out', name_re=r'\.[ab]$', limit=1)
    assert res['count'] == 2 and res['truncated'] is True
    with pytest.raises(ValueError, match='not found'):
        twister.twister_results(cfg, 'nope')


def test_twister_run_and_build_end_to_end(cfg):
    # Replace west by a script that writes a report and prints a ticker.
    out = cfg.topdir / 'twister-out'
    script = write(
        cfg.topdir / 'fake_west.py',
        f'''\
import json, sys
from pathlib import Path
print("INFO    - 2 test scenarios (3 configurations) selected, 1 configurations filtered")
print("INFO    - Total complete:    3/   3  100%  built (not run): 0, filtered: 1, "
      "failed: 1, error: 0", end="\\r")
out = Path({str(out)!r}); out.mkdir()
(out / "twister.json").write_text({json.dumps(REPORT)!r})
sys.exit(1)
''',
    )
    progress = []

    async def report(progress_value, total=None, message=None):
        progress.append((progress_value, total))

    with patch(
        'zephyr_mcp.twister.west_argv', side_effect=lambda *a: [sys.executable, str(script)]
    ):
        res = asyncio.run(twister.twister_run(cfg, report, paths=['.'], platforms=['native_sim']))
    assert res['ok'] is False and res['returncode'] == 1
    assert res['summary']['total'] == 3
    assert [s['name'] for s in res['failed']] == ['k.b']
    assert res['twister_json'] == out / 'twister.json'
    assert progress == [(0, 3), (3, 3)]

    app = cfg.topdir / 'app'
    app.mkdir()
    script = write(
        cfg.topdir / 'fake_build.py',
        '''\
print("[1/3] Building C object main.c.obj")
print("/ws/app/src/main.c:1:2: error: nope")
print("ninja: build stopped: subcommand failed.")
raise SystemExit(2)
''',
    )
    with patch('zephyr_mcp.build.west_argv', side_effect=lambda *a: [sys.executable, str(script)]):
        res = asyncio.run(build_mod.build(cfg, None, 'app', 'native_sim'))
    assert res['ok'] is False and res['returncode'] == 2
    assert res['build_dir'] == cfg.topdir / 'build'
    assert [d['severity'] for d in res['diagnostics']] == ['error', 'error']
    with pytest.raises(ValueError, match='pristine'):
        asyncio.run(build_mod.build(cfg, None, 'app', pristine='sometimes'))
    with pytest.raises(ValueError, match='outside'):
        asyncio.run(build_mod.build(cfg, None, '/etc'))


# --- command and server --------------------------------------------------


def test_mcp_cmd_parser_and_missing_sdk():
    cmd = Mcp()
    cmd.config = _FakeConfig({'color.ui': False})
    cmd.topdir = '/ws'
    cmd.manifest = FakeManifest()
    import argparse

    parser = argparse.ArgumentParser(allow_abbrev=False)
    cmd.parser = cmd.do_add_parser(parser.add_subparsers())
    args = parser.parse_args(['mcp', '--allow-hardware', '--root', '/a', '--root', '/b'])
    assert args.allow_hardware and args.root == ['/a', '/b'] and args.log_dir is None
    with patch.dict(sys.modules, {'zephyr_mcp.server': None}), pytest.raises(SystemExit):
        cmd.do_run(args, [])


def _run(coro):
    return asyncio.run(coro)


@pytest.fixture
def mcp_sdk():
    return pytest.importorskip('mcp')


def _tools(result):
    return getattr(result, 'tools', result)


def test_server_tools_and_gating(cfg, mcp_sdk):
    from mcp import Client

    from zephyr_mcp import server

    async def names(allow_hardware):
        cfg.allow_hardware = allow_hardware
        async with Client(server.build_server(cfg)) as client:
            tools = _tools(await client.list_tools())
            return {t.name: t for t in tools}

    tools = _run(names(False))
    assert 'flash' not in tools
    assert {'workspace_info', 'build', 'twister_run', 'devicetree_query'} <= set(tools)
    assert tools['workspace_info'].annotations.read_only_hint is True
    assert tools['build'].annotations.read_only_hint is False
    assert 'source_dir' in tools['build'].input_schema['required']
    assert 'ctx' not in tools['build'].input_schema['properties']
    tools = _run(names(True))
    assert tools['flash'].annotations.destructive_hint is True


def test_server_round_trip(cfg, mcp_sdk):
    from mcp import Client

    from zephyr_mcp import server

    make_build_dir(cfg.topdir / 'build')

    async def go():
        async with Client(server.build_server(cfg)) as client:
            info = await client.call_tool('workspace_info', {})
            assert info.is_error is False
            assert info.structured_content['topdir'] == str(cfg.topdir)
            kconf = await client.call_tool('kconfig', {'build_dir': 'build', 'symbols': ['GPIO']})
            assert kconf.structured_content['values'] == {'CONFIG_GPIO': 'y'}
            bad = await client.call_tool('kconfig', {'build_dir': '/etc', 'symbols': ['X']})
            assert bad.is_error is True
            res = await client.read_resource('zephyr://workspace')
            text = getattr(res, 'contents', res)[0].text
            assert json.loads(text)['topdir'] == str(cfg.topdir)
            res = await client.read_resource('zephyr://build/build/config')
            assert 'CONFIG_GPIO=y' in getattr(res, 'contents', res)[0].text

    _run(go())
