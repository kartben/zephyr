# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import os
from pathlib import Path
from unittest.mock import patch

import pytest
from conftest import _FakeConfig

from blobs import Blobs


def parse(argv):
    cmd = Blobs()
    cmd.config = _FakeConfig()
    parser = argparse.ArgumentParser(allow_abbrev=False)
    cmd.parser = cmd.do_add_parser(parser.add_subparsers())
    return cmd, parser.parse_args(['blobs', *argv])


BLOBS = [
    {
        'module': 'hal_vnd',
        'path': 'lib/libvnd.a',
        'abspath': Path('/z/modules/hal/vnd/zephyr/blobs/lib/libvnd.a'),
        'sha256': 'ab' * 32,
        'type': 'lib',
        'version': '1.0',
        'license-path': 'LICENSE',
        'license-abspath': Path('/z/modules/hal/vnd/LICENSE'),
        'url': 'https://example.com/libvnd.a',
        'description': 'vendor lib',
        'doc-url': 'https://example.com/doc',
        'click-through': False,
        'status': 'D',
    }
]


def run_blobs(argv):
    cmd, args = parse(argv)
    with patch.object(Blobs, 'get_blobs', return_value=BLOBS):
        cmd.do_run(args, [])


def test_json_output(capsys):
    run_blobs(['list', '--json'])
    out = json.loads(capsys.readouterr().out)
    assert len(out) == 1
    assert out[0]['module'] == 'hal_vnd'
    assert out[0]['abspath'] == os.fspath(BLOBS[0]['abspath'])
    assert out[0]['license-abspath'] == os.fspath(BLOBS[0]['license-abspath'])
    assert out[0]['status'] == 'D'
    assert out[0]['click-through'] is False


def test_json_and_format_are_exclusive():
    cmd, args = parse(['list', '--json', '-f', '{module}'])
    with pytest.raises(SystemExit):
        cmd.do_run(args, [])


def test_json_only_valid_for_list():
    cmd, args = parse(['fetch', '--json'])
    with pytest.raises(SystemExit):
        cmd.do_run(args, [])


def test_text_output_unchanged(capsys):
    run_blobs(['list', '-f', '{module} {status} {path}'])
    assert capsys.readouterr().out == 'hal_vnd D lib/libvnd.a\n'
