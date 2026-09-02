# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import os
from unittest.mock import patch

import pytest
from conftest import _FakeConfig

from sdk import Sdk, sdk_entry_from_path


def make_sdk(root, version='1.0.0', toolchains=('arm-zephyr-eabi', 'riscv64-zephyr-elf')):
    root.mkdir(parents=True)
    (root / 'sdk_version').write_text(f'{version}\n')
    (root / 'hosttools').mkdir()
    for tc in toolchains:
        gcc = root / 'gnu' / tc / 'bin' / f'{tc}-gcc'
        gcc.parent.mkdir(parents=True)
        gcc.write_text('')
    # A directory without a compiler is not a toolchain.
    (root / 'gnu' / 'not-a-toolchain' / 'bin').mkdir(parents=True)
    (root / 'sdk_gnu_toolchains').write_text(
        'arm-zephyr-eabi\nriscv64-zephyr-elf\nxtensa-espressif_esp32_zephyr-elf\n'
    )
    return root


@pytest.mark.skipif(os.name == 'nt', reason='gcc suffix differs on Windows')
def test_sdk_entry_from_path(tmp_path):
    root = make_sdk(tmp_path / 'zephyr-sdk-1.0.0')
    entry = sdk_entry_from_path(root)
    assert entry == {
        'version': '1.0.0',
        'path': root,
        'hosttools': True,
        'llvm': False,
        'gnu_toolchains': ['arm-zephyr-eabi', 'riscv64-zephyr-elf'],
        'gnu_available_toolchains': ['xtensa-espressif_esp32_zephyr-elf'],
    }
    (root / 'llvm').mkdir()
    assert sdk_entry_from_path(root)['llvm'] is True
    assert sdk_entry_from_path(tmp_path / 'nope') is None


def parse(argv):
    cmd = Sdk()
    cmd.config = _FakeConfig()
    parser = argparse.ArgumentParser(allow_abbrev=False)
    cmd.parser = cmd.do_add_parser(parser.add_subparsers())
    return cmd, parser.parse_args(['sdk', *argv])


def run_sdk(argv, entries):
    cmd, args = parse(argv)
    with patch.object(Sdk, 'fetch_sdk_info', return_value={e['version']: e for e in entries}):
        cmd.do_run(args, [])


@pytest.mark.skipif(os.name == 'nt', reason='gcc suffix differs on Windows')
def test_list_json(tmp_path, capsys):
    entry = sdk_entry_from_path(make_sdk(tmp_path / 'sdk'))
    run_sdk(['list', '--json'], [entry])
    out = json.loads(capsys.readouterr().out)
    assert out[0]['version'] == '1.0.0'
    assert out[0]['path'] == os.fspath(tmp_path / 'sdk')
    assert out[0]['gnu_toolchains'] == ['arm-zephyr-eabi', 'riscv64-zephyr-elf']


def test_list_json_empty(capsys):
    run_sdk(['list', '--json'], [])
    assert json.loads(capsys.readouterr().out) == []


def test_list_text_empty_dies():
    with pytest.raises(SystemExit):
        run_sdk([], [])


@pytest.mark.skipif(os.name == 'nt', reason='gcc suffix differs on Windows')
def test_list_text(tmp_path, capsys):
    entry = sdk_entry_from_path(make_sdk(tmp_path / 'sdk'))
    run_sdk(['list'], [entry])
    assert capsys.readouterr().out.splitlines() == [
        '1.0.0:',
        f'  path: {tmp_path / "sdk"}',
        '  hosttools: installed',
        '  gnu-installed-toolchains:',
        '    - arm-zephyr-eabi',
        '    - riscv64-zephyr-elf',
        '  gnu-available-toolchains:',
        '    - xtensa-espressif_esp32_zephyr-elf',
        '',
    ]
