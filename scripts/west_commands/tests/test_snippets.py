# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import os
from pathlib import Path
from unittest.mock import patch

import pytest
from conftest import _FakeConfig
from snippets import Snippet

from snippets_cmd import Snippets, snippet_to_dict


def parse(argv):
    cmd = Snippets()
    cmd.config = _FakeConfig()
    parser = argparse.ArgumentParser(allow_abbrev=False)
    cmd.parser = cmd.do_add_parser(parser.add_subparsers())
    return cmd, parser.parse_args(['snippets', *argv])


def make_snippets():
    rtt = Snippet('rtt-console', dirs=[Path('/z/snippets/rtt-console')], description='Use RTT')
    rtt.board2appends['nrf52840dk']['']['EXTRA_CONF_FILE'].append('x.conf')
    rtt.board2appends['/nrf.*/']['']['EXTRA_CONF_FILE'].append('y.conf')
    bare = Snippet('bare', dirs=[Path('/z/snippets/bare'), Path('/ext/snippets/bare')])
    return {'rtt-console': rtt, 'bare': bare}


def run_snippets(argv):
    cmd, args = parse(argv)
    with (
        patch('snippets_cmd.run_cmake', return_value=[]),
        patch('snippets_cmd.find_snippets_in_roots', return_value=make_snippets()),
    ):
        cmd.do_run(args, [])


def test_snippet_to_dict():
    d = snippet_to_dict(make_snippets()['rtt-console'])
    assert d == {
        'name': 'rtt-console',
        'description': 'Use RTT',
        'dirs': [Path('/z/snippets/rtt-console')],
        'boards': ['/nrf.*/', 'nrf52840dk'],
    }
    assert snippet_to_dict(make_snippets()['bare'])['description'] is None


def test_json_and_format_are_exclusive():
    cmd, args = parse(['--json', '-f', '{name}'])
    with pytest.raises(SystemExit):
        cmd.do_run(args, [])


def test_json_output(capsys):
    run_snippets(['--json'])
    out = json.loads(capsys.readouterr().out)
    assert [s['name'] for s in out] == ['bare', 'rtt-console']
    assert out[0]['dirs'] == [
        os.fspath(Path('/z/snippets/bare')),
        os.fspath(Path('/ext/snippets/bare')),
    ]
    assert out[0]['boards'] == []

    run_snippets(['--json', '-n', 'rtt'])
    assert [s['name'] for s in json.loads(capsys.readouterr().out)] == ['rtt-console']


def test_text_output_unchanged(capsys):
    run_snippets(['-f', '{name}'])
    assert capsys.readouterr().out == 'bare\nrtt-console\n'
