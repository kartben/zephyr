# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

'''Tests for the hardware listing cache.

Most of these pin down invalidation: which edits must be noticed by the next
listing, and, just as importantly, which ones must not cost a reparse.
'''

import argparse
import sys
from collections import Counter
from pathlib import Path

import pytest

ZEPHYR_BASE = Path(__file__).parents[3]
sys.path.insert(0, str(ZEPHYR_BASE / 'scripts'))

import list_boards  # noqa: E402
import list_cache  # noqa: E402
import list_hardware  # noqa: E402
import list_shields  # noqa: E402

BOARD_WITH_VARIANT_YML = '''\
board:
  name: plank
  full_name: Plank Board
  vendor: acme
  socs:
    - name: fakesoc
      variants:
        - name: ns
          cpucluster: cpuapp
'''

SOC_YML = '''\
family:
  - name: fakefamily
    series:
      - name: fakeseries
        socs:
          - name: fakesoc
            cpuclusters:
              - name: cpuapp
'''

SOC_WITH_SECOND_CLUSTER_YML = SOC_YML + '''\
              - name: cpunet
'''

SHIELD_YML = '''\
shield:
  name: hat
  full_name: Fake Hat
  vendor: acme
'''


def board_yml(name):
    return f'''\
board:
  name: {name}
  full_name: {name} board
  vendor: acme
  socs:
    - name: fakesoc
'''


def write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)


@pytest.fixture
def tree(tmp_path, monkeypatch):
    '''A minimal board and SoC root, with a cache directory of its own.'''
    write(tmp_path / 'boards/acme/plank/board.yml', board_yml('plank'))
    write(tmp_path / 'boards/acme/plank/plank.dts', '/dts-v1/;\n')
    write(tmp_path / 'boards/shields/hat/shield.yml', SHIELD_YML)
    write(tmp_path / 'soc/acme/fakesoc/soc.yml', SOC_YML)
    write(tmp_path / 'soc/acme/fakesoc/fakesoc.dtsi', '/ { soc { }; };\n')

    monkeypatch.setenv('ZEPHYR_LIST_CACHE_DIR', str(tmp_path / 'cache'))
    monkeypatch.delenv('ZEPHYR_LIST_CACHE_DISABLE', raising=False)
    return tmp_path


@pytest.fixture
def parses(monkeypatch):
    '''Counts how often each listing parses its input files.

    These functions only run on a cache miss, so a count of zero means the
    listing was served from the cache.
    '''
    counts = Counter()

    def count(name, owner, attr):
        original = getattr(owner, attr)

        def counting(*args, **kwargs):
            counts[name] += 1
            return original(*args, **kwargs)

        monkeypatch.setattr(owner, attr, counting)

    count('boards', list_boards, 'load_v2_boards')
    count('systems', list_hardware.Systems, 'from_file')
    count('shields', list_shields, 'find_shields_in')
    return counts


def args_for(tree, board=None):
    return argparse.Namespace(board_roots=[tree], soc_roots=[tree], board=board, board_dir=[])


def boards(tree, board=None):
    '''List the boards, as a summary that covers their SoCs and variants.'''
    found = list_boards.find_v2_boards(args_for(tree, board))
    return {name: list_boards.board_v2_qualifiers(b) for name, b in found.items()}


def socs(tree):
    systems = list_hardware.find_v2_systems(args_for(tree))
    return sorted(s.name for s in systems.get_socs())


def shields(tree):
    return sorted(s.name for s in list_shields.find_shields(args_for(tree)))


def test_listings_are_served_from_the_cache(tree, parses):
    expected = (boards(tree), socs(tree), shields(tree))
    parses.clear()

    assert (boards(tree), socs(tree), shields(tree)) == expected
    assert parses.total() == 0


def test_editing_a_board_invalidates_it(tree, parses):
    assert boards(tree)['plank'] == ['fakesoc/cpuapp']
    parses.clear()

    write(tree / 'boards/acme/plank/board.yml', BOARD_WITH_VARIANT_YML)
    assert boards(tree)['plank'] == ['fakesoc/cpuapp', 'fakesoc/cpuapp/ns']
    assert parses['boards'] > 0


def test_adding_a_board_invalidates_the_listing(tree):
    write(tree / 'boards/acme/deck/board.yml', board_yml('deck'))
    assert sorted(boards(tree)) == ['deck', 'plank']


def test_removing_a_board_invalidates_the_listing(tree):
    assert 'plank' in boards(tree)

    (tree / 'boards/acme/plank/board.yml').unlink()
    assert boards(tree) == {}


def test_editing_a_soc_invalidates_boards_too(tree, parses):
    assert boards(tree)['plank'] == ['fakesoc/cpuapp']
    parses.clear()

    write(tree / 'soc/acme/fakesoc/soc.yml', SOC_WITH_SECOND_CLUSTER_YML)
    assert boards(tree)['plank'] == ['fakesoc/cpuapp', 'fakesoc/cpunet']
    assert parses['boards'] > 0


def test_editing_devicetree_invalidates_nothing(tree, parses):
    expected = (boards(tree), socs(tree), shields(tree))
    parses.clear()

    # Devicetree is not read by any listing, so it cannot be an input to one.
    write(tree / 'boards/acme/plank/plank.dts', '/dts-v1/;\n/ { model = "plank"; };\n')
    write(tree / 'soc/acme/fakesoc/fakesoc.dtsi', '/ { soc { reg = <0 1>; }; };\n')

    assert (boards(tree), socs(tree), shields(tree)) == expected
    assert parses.total() == 0


def test_rewriting_a_board_unchanged_invalidates_nothing(tree, parses):
    expected = boards(tree)
    parses.clear()

    # Contents decide, not timestamps: switching branches back and forth or
    # regenerating a file must not throw the cache away.
    write(tree / 'boards/acme/plank/board.yml', board_yml('plank'))
    assert boards(tree) == expected
    assert parses['boards'] == 0


def test_changing_the_listing_code_invalidates_everything(tree, parses, monkeypatch):
    source = tree / 'fake_lister.py'
    write(source, '# v1\n')
    monkeypatch.setattr(list_cache, '_CODE_INPUTS', (*list_cache._CODE_INPUTS, source))

    boards(tree)
    parses.clear()

    write(source, '# v2\n')
    boards(tree)
    assert parses['boards'] > 0


def test_a_board_filter_is_part_of_the_key(tree):
    write(tree / 'boards/acme/deck/board.yml', board_yml('deck'))

    assert sorted(boards(tree)) == ['deck', 'plank']
    assert sorted(boards(tree, board='deck')) == ['deck']
    assert sorted(boards(tree)) == ['deck', 'plank']


def test_editing_a_shield_invalidates_it(tree, parses):
    assert shields(tree) == ['hat']
    parses.clear()

    write(tree / 'boards/shields/hat/shield.yml', SHIELD_YML.replace('hat', 'cap'))
    assert shields(tree) == ['cap']
    assert parses['shields'] > 0


def test_adding_a_shield_without_a_shield_yml_invalidates_the_listing(tree):
    # Shields that predate shield.yml are named after their .overlay files.
    write(tree / 'boards/shields/relay/Kconfig.shield', 'config SHIELD_RELAY\n')
    write(tree / 'boards/shields/relay/relay.overlay', '/ { };\n')

    assert shields(tree) == ['hat', 'relay']


def test_an_unusable_entry_is_a_miss(tree):
    expected = boards(tree)

    for entry in (tree / 'cache').glob('boards-*.pickle'):
        entry.write_bytes(b'not a pickle')

    assert boards(tree) == expected


def test_an_unwritable_cache_directory_is_harmless(tree, monkeypatch, capsys):
    blocker = tree / 'not-a-directory'
    write(blocker, '')
    monkeypatch.setenv('ZEPHYR_LIST_CACHE_DIR', str(blocker / 'cache'))

    assert 'plank' in boards(tree)
    assert capsys.readouterr().err == ''


def test_the_cache_can_be_disabled(tree, parses, monkeypatch):
    boards(tree)
    parses.clear()

    monkeypatch.setenv('ZEPHYR_LIST_CACHE_DISABLE', '1')
    boards(tree)
    assert parses['boards'] > 0
    assert list_cache.cache_dir() is None


def test_entries_do_not_pile_up(tree):
    for i in range(list_cache.MAX_ENTRIES_PER_NAMESPACE * 2):
        write(tree / 'boards/acme/plank/board.yml', f'{board_yml("plank")}# revision {i}\n')
        boards(tree)

    entries = list((tree / 'cache').glob('boards-*.pickle'))
    assert len(entries) == list_cache.MAX_ENTRIES_PER_NAMESPACE
