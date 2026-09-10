# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import os
from unittest.mock import patch

import pytest
from conftest import _FakeConfig

import run_common
from runners import get_runner_cls
from runners_cmd import Runners


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def make_build_dir(root, board='native_sim', runners=True):
    write(root / 'zephyr' / '.config', f'CONFIG_BOARD_TARGET="{board}"\nCONFIG_FOO=y\n')
    if runners:
        write(
            root / 'zephyr' / 'runners.yaml',
            '''\
runners:
- bossac
- openocd
flash-runner: bossac
debug-runner: openocd
config:
  board_dir: /z/boards/vnd/plain
  elf_file: zephyr.elf
  bin_file: zephyr.bin
  gdb: /sdk/bin/gdb
  openocd_search:
    - /sdk/openocd/scripts
args:
  bossac:
    - --bossac=/usr/bin/bossac
  openocd: []
''',
        )
    return root


def test_runner_class_info():
    info = run_common.runner_class_info(get_runner_cls('openocd'))
    assert info['name'] == 'openocd'
    assert 'flash' in info['commands'] and info['commands'] == sorted(info['commands'])
    assert info['capabilities']['commands'] == set(info['commands'])
    flags = [f for opt in info['options'] for f in opt['flags']]
    assert '--config' in flags
    # No positional (the 'command' argument) is reported.
    assert all(opt['flags'] for opt in info['options'])
    # Common capability-driven options are reported too.
    assert '--dev-id' in flags


def test_build_runner_context(tmp_path):
    build_dir = make_build_dir(tmp_path / 'build')
    ctx = run_common.build_runner_context(build_dir)
    assert ctx['build_dir'] == os.fspath(build_dir)
    assert ctx['sysbuild'] is False
    [domain] = ctx['domains']
    assert domain['name'] is None
    assert domain['board'] == 'native_sim'
    assert domain['runners_yaml'] == os.fspath(build_dir / 'zephyr' / 'runners.yaml')
    assert domain['available'] == ['bossac', 'openocd']
    assert domain['default'] == {'flash': 'bossac', 'debug': 'openocd'}
    assert domain['config']['elf_file'] == os.fspath(build_dir / 'zephyr' / 'zephyr.elf')
    assert domain['config']['hex_file'] is None
    assert domain['config']['file_type'] == 'OTHER'
    assert domain['config']['openocd_search'] == ['/sdk/openocd/scripts']
    assert domain['args'] == {'bossac': ['--bossac=/usr/bin/bossac'], 'openocd': []}


def test_build_runner_context_without_runners_yaml(tmp_path):
    build_dir = make_build_dir(tmp_path / 'build', runners=False)
    [domain] = run_common.build_runner_context(build_dir)['domains']
    assert domain['runners_yaml'] is None
    assert domain['available'] == [] and domain['config'] is None


def test_build_runner_context_sysbuild(tmp_path):
    top = tmp_path / 'build'
    make_build_dir(top / 'app', board='plain/soc1')
    make_build_dir(top / 'mcuboot', board='plain/soc1', runners=False)
    write(
        top / 'domains.yaml',
        f'''\
default: app
build_dir: {top}
domains:
  - name: app
    build_dir: {top / 'app'}
  - name: mcuboot
    build_dir: {top / 'mcuboot'}
flash_order:
  - mcuboot
  - app
''',
    )
    ctx = run_common.build_runner_context(top)
    assert ctx['sysbuild'] is True
    assert [d['name'] for d in ctx['domains']] == ['app', 'mcuboot']
    assert ctx['domains'][0]['available'] == ['bossac', 'openocd']
    assert ctx['domains'][1]['runners_yaml'] is None
    ctx = run_common.build_runner_context(top, ['mcuboot'])
    assert [d['name'] for d in ctx['domains']] == ['mcuboot']


def parse(argv):
    cmd = Runners()
    cmd.config = _FakeConfig({'color.ui': False})
    parser = argparse.ArgumentParser(allow_abbrev=False)
    cmd.parser = cmd.do_add_parser(parser.add_subparsers())
    return cmd, parser.parse_args(['runners', *argv])


def run_runners(argv, build_dir=None):
    cmd, args = parse(argv)
    with (
        patch('runners_cmd.import_module_runners'),
        patch('runners_cmd.get_build_dir', return_value=build_dir),
    ):
        cmd.do_run(args, [])


def test_json_without_build_dir(capsys):
    run_runners(['--json', '-r', 'openocd'])
    out = json.loads(capsys.readouterr().out)
    assert out['build_dir'] is None and out['domains'] == []
    assert [r['name'] for r in out['runners']] == ['openocd']
    assert out['runners'][0]['capabilities']['commands'] == out['runners'][0]['commands']


def test_json_with_build_dir(tmp_path, capsys):
    build_dir = make_build_dir(tmp_path / 'build')
    run_runners(['--json', '-d', os.fspath(build_dir)], build_dir=os.fspath(build_dir))
    out = json.loads(capsys.readouterr().out)
    assert out['build_dir'] == os.fspath(build_dir)
    assert out['domains'][0]['default'] == {'debug': 'openocd', 'flash': 'bossac'}
    assert len(out['runners']) > 10


def test_unknown_runner_dies():
    with pytest.raises(SystemExit):
        run_runners(['-r', 'no-such-runner'])


def test_text_output(tmp_path, capsys):
    build_dir = make_build_dir(tmp_path / 'build')
    run_runners(['-d', os.fspath(build_dir), '-r', 'bossac'], build_dir=os.fspath(build_dir))
    out = capsys.readouterr().out
    assert out.startswith('runners:\n  bossac: flash')
    assert 'available runners: bossac, openocd' in out
    assert 'default runners: flash=bossac, debug=openocd' in out
