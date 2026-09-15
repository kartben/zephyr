# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import os
from pathlib import Path
from unittest.mock import patch

import list_boards
import pytest
from conftest import _FakeConfig
from list_boards import Board, Cpucluster, Revision, Soc, Variant

from boards import Boards, board_targets, board_to_dict


def make_parser():
    cmd = Boards()
    cmd.config = _FakeConfig()
    parser = argparse.ArgumentParser(allow_abbrev=False)
    cmd.parser = cmd.do_add_parser(parser.add_subparsers())
    return cmd, parser


def parse(argv):
    cmd, parser = make_parser()
    return cmd, parser.parse_args(['boards', *argv])


def simple_board(name='plain', **kwargs):
    defaults = dict(
        directories=[Path('/z/boards/vnd') / name],
        hwm='v2',
        vendor='vnd',
        socs=[Soc('soc1')],
    )
    defaults.update(kwargs)
    return Board(name=name, **defaults)


def nested_board():
    soc = Soc(
        'nrf5340',
        cpuclusters=[
            Cpucluster('cpuapp', [Variant('ns', [Variant('xip')])]),
            Cpucluster('cpunet'),
        ],
    )
    return Board(
        name='nrf5340dk',
        directories=[Path('/z/boards/nordic/nrf5340dk'), Path('/ext/boards/nrf5340dk')],
        hwm='v2',
        full_name='nRF5340 DK',
        vendor='nordic',
        revision_format='major.minor.patch',
        revision_default='0.11.0',
        revisions=[Revision('0.11.0'), Revision('0.12.0')],
        socs=[soc],
        variants=[Variant('bt')],
    )


def test_targets_match_all_targets_output():
    board = nested_board()
    qualifiers = list_boards.board_v2_qualifiers(board)
    assert qualifiers == [
        'nrf5340/cpuapp',
        'nrf5340/cpuapp/ns',
        'nrf5340/cpuapp/ns/xip',
        'nrf5340/cpunet',
        'bt',
    ]
    targets = board_targets(board)
    assert targets[: len(qualifiers)] == [f'nrf5340dk/{q}' for q in qualifiers]
    assert 'nrf5340dk@0.12.0/nrf5340/cpuapp/ns' in targets
    assert len(targets) == len(qualifiers) * 3
    # No revisions: only plain targets.
    assert board_targets(simple_board()) == ['plain/soc1']


def test_board_to_dict_round_trips_through_json():
    from zephyr_ext_common import ZephyrJSONEncoder

    d = json.loads(json.dumps(board_to_dict(nested_board()), cls=ZephyrJSONEncoder))
    assert d['name'] == 'nrf5340dk'
    assert d['dir'] == os.fspath(Path('/z/boards/nordic/nrf5340dk'))
    assert d['dirs'] == [
        os.fspath(Path('/z/boards/nordic/nrf5340dk')),
        os.fspath(Path('/ext/boards/nrf5340dk')),
    ]
    assert d['revisions'] == [
        {'name': '0.11.0', 'variants': []},
        {'name': '0.12.0', 'variants': []},
    ]
    assert d['socs'][0]['cpuclusters'][0] == {
        'name': 'cpuapp',
        'variants': [{'name': 'ns', 'variants': [{'name': 'xip', 'variants': []}]}],
    }
    assert d['variants'] == [{'name': 'bt', 'variants': []}]
    assert d['qualifiers'][0] == 'nrf5340/cpuapp'
    assert d['targets'][-1] == 'nrf5340dk@0.12.0/bt'
    assert d['revision_exact'] is False
    assert 'arch' not in d


def test_board_to_dict_single_directory():
    board = simple_board(directories=Path('/z/boards/vnd/plain'))
    d = board_to_dict(board)
    assert d['dir'] == Path('/z/boards/vnd/plain')
    assert d['dirs'] == [Path('/z/boards/vnd/plain')]


def test_json_and_format_are_exclusive():
    cmd, args = parse(['--json', '-f', '{name}'])
    with pytest.raises(SystemExit):
        cmd.do_run(args, [])


def run_boards(argv, boards):
    cmd, args = parse(argv)
    cmd.manifest = object()
    roots = {'arch_root': [], 'board_root': [], 'soc_root': []}
    with (
        patch('boards.module_roots', return_value=roots),
        patch('list_boards.find_v2_boards', return_value={b.name: b for b in boards}),
    ):
        cmd.do_run(args, [])


def test_json_output_is_sorted_and_filtered(capsys):
    boards = [simple_board('zeta'), nested_board(), simple_board('alpha')]
    run_boards(['--json'], boards)
    out = json.loads(capsys.readouterr().out)
    assert [b['name'] for b in out] == ['alpha', 'nrf5340dk', 'zeta']

    run_boards(['--json', '-n', '^nrf'], boards)
    out = json.loads(capsys.readouterr().out)
    assert [b['name'] for b in out] == ['nrf5340dk']


def test_json_all_targets(capsys):
    boards = [simple_board('zeta'), simple_board('alpha')]
    run_boards(['--json', '--all-targets'], boards)
    assert json.loads(capsys.readouterr().out) == ['alpha/soc1', 'zeta/soc1']


def test_text_output_unchanged(capsys):
    run_boards(['-f', '{name}:{qualifiers}'], [simple_board('alpha')])
    assert capsys.readouterr().out == 'alpha:soc1\n'
    run_boards([], [simple_board('alpha')])
    assert capsys.readouterr().out == 'alpha\n'
