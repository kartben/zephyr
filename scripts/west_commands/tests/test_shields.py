# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import os
from pathlib import Path
from unittest.mock import patch

import pytest
from conftest import _FakeConfig
from list_shields import Shield

from shields import Shields, shield_to_dict


def parse(argv):
    cmd = Shields()
    cmd.config = _FakeConfig()
    cmd.manifest = object()
    parser = argparse.ArgumentParser(allow_abbrev=False)
    cmd.parser = cmd.do_add_parser(parser.add_subparsers())
    return cmd, parser.parse_args(['shields', *argv])


SHIELDS = [
    Shield(
        'x_nucleo_iks01a1', Path('/z/boards/shields/x_nucleo_iks01a1'), 'X-NUCLEO', 'st', ['i2c']
    ),
    Shield('adafruit_2_8_tft', Path('/z/boards/shields/adafruit_2_8_tft'), None, None, []),
]


def run_shields(argv):
    cmd, args = parse(argv)
    with (
        patch('shields.module_roots', return_value={'board_root': []}),
        patch('list_shields.find_shields', return_value=sorted(SHIELDS, key=lambda s: s.name)),
    ):
        cmd.do_run(args, [])


def test_shield_to_dict():
    d = shield_to_dict(SHIELDS[0])
    assert d == {
        'name': 'x_nucleo_iks01a1',
        'full_name': 'X-NUCLEO',
        'vendor': 'st',
        'dir': Path('/z/boards/shields/x_nucleo_iks01a1'),
        'supported_features': ['i2c'],
    }


def test_json_and_format_are_exclusive():
    cmd, args = parse(['--json', '-f', '{name}'])
    with pytest.raises(SystemExit):
        cmd.do_run(args, [])


def test_json_output(capsys):
    run_shields(['--json'])
    out = json.loads(capsys.readouterr().out)
    assert [s['name'] for s in out] == ['adafruit_2_8_tft', 'x_nucleo_iks01a1']
    assert out[0]['full_name'] is None
    assert out[1]['dir'] == os.fspath(Path('/z/boards/shields/x_nucleo_iks01a1'))

    run_shields(['--json', '-n', 'nucleo'])
    assert [s['name'] for s in json.loads(capsys.readouterr().out)] == ['x_nucleo_iks01a1']


def test_text_output_unchanged(capsys):
    run_shields([])
    assert capsys.readouterr().out == 'adafruit_2_8_tft\nx_nucleo_iks01a1\n'
    run_shields(['-f', '{name}:{vendor}'])
    assert capsys.readouterr().out == 'adafruit_2_8_tft:None\nx_nucleo_iks01a1:st\n'
