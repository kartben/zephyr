#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Tests for board_description.py.

The devicetree itself comes from the Zephyr CMake package, so what is tested
here is the command line handed to it and the mapping of what it returns
onto the description.
"""

import os
import sys
from pathlib import Path

import pytest

sys.path.insert(0, os.path.join(os.environ["ZEPHYR_BASE"], "scripts"))
import board_description as iut  # Implementation Under Test
import list_boards

WORKSPACE = Path('/workspace')
ZEPHYR = WORKSPACE / 'zephyr'


def make_board(name, socs, revision_format=None, revisions=(), default=None):
    return list_boards.Board(
        name=name,
        directories=[ZEPHYR / 'boards' / name],
        hwm='v2',
        full_name=f'{name} board',
        vendor='acme',
        revision_format=revision_format,
        revision_default=default,
        revision_exact=False,
        revisions=[list_boards.Revision(r) for r in revisions],
        socs=[list_boards.Soc(s, variants=[list_boards.Variant('ns')]) for s in socs],
    )


BOARDS = {
    'plank': make_board('plank', ['soc1']),
    'mmp': make_board('mmp', ['soc1', 'soc2'], 'major.minor.patch', ['0.7.0', '1.2.3'], '0.7.0'),
}


@pytest.fixture
def describer(monkeypatch):
    monkeypatch.setattr(iut.shutil, 'which', lambda name: f'/usr/bin/{name}')
    return iut.BoardDescriber(BOARDS, workspace_dir=WORKSPACE)


def build_info(name='plank', qualifiers='soc1', revision='', **devicetree):
    return {
        'board': {
            'name': name,
            'qualifiers': qualifiers,
            'revision': revision,
            'path': [str(ZEPHYR / 'boards' / name)],
        },
        'devicetree': devicetree,
    }


def test_cmake_command(monkeypatch, tmp_path):
    """The devicetree is asked for through the Zephyr CMake package."""
    monkeypatch.setattr(iut.shutil, 'which', lambda name: f'/usr/bin/{name}')
    describer = iut.BoardDescriber(
        BOARDS,
        roots={'BOARD_ROOT': [Path('/extra')], 'SOC_ROOT': []},
        preprocessor='my-cpp',
    )

    captured = []
    monkeypatch.setattr(
        iut.subprocess,
        'run',
        lambda cmd, **kwargs: captured.append(cmd) or iut.subprocess.CompletedProcess(cmd, 0),
    )
    describer._run_cmake('plank/soc1', tmp_path)

    cmd = captured[0]
    assert cmd[0] == '/usr/bin/cmake'
    assert '-DBOARD=plank/soc1' in cmd
    assert '-DMODULES=dts' in cmd
    assert '-DCMAKE_DTS_PREPROCESSOR=my-cpp' in cmd
    assert '-DBOARD_ROOT=/extra' in cmd
    # Empty roots are not passed on, and neither are unrelated ones.
    assert not any(a.startswith('-DSOC_ROOT') for a in cmd)
    assert cmd[-2:] == ['-P', str(iut.PACKAGE_HELPER)]
    # An empty application, so that no application overlay is picked up.
    source = cmd[cmd.index('-S') + 1]
    assert list(Path(source).iterdir()) == [Path(source) / 'prj.conf']


def test_cmake_failure(monkeypatch, tmp_path):
    monkeypatch.setattr(iut.shutil, 'which', lambda name: f'/usr/bin/{name}')
    describer = iut.BoardDescriber(BOARDS)
    monkeypatch.setattr(
        iut.subprocess,
        'run',
        lambda cmd, **kwargs: iut.subprocess.CompletedProcess(cmd, 1, '', 'No board named ...'),
    )
    with pytest.raises(iut.BoardDescriptionError, match='No board named'):
        describer._run_cmake('nosuch', tmp_path)


@pytest.mark.parametrize(
    'requested, name, qualifiers, revision, target, soc',
    [
        ('plank', 'plank', 'soc1', '', 'plank/soc1', 'soc1'),
        ('plank//ns', 'plank', 'soc1/ns', '', 'plank/soc1/ns', 'soc1'),
        ('mmp/soc2', 'mmp', 'soc2', '', 'mmp/soc2', 'soc2'),
        ('mmp@1', 'mmp', 'soc1', '0.7.0', 'mmp@0.7.0/soc1', 'soc1'),
    ],
)
def test_board_description(describer, requested, name, qualifiers, revision, target, soc):
    description = describer._board_description(
        requested, build_info(name, qualifiers, revision)['board']
    )
    assert description['name'] == name
    assert description['target'] == target
    assert description['requested_target'] == requested
    assert description['normalized_target'] == f'{name}/{qualifiers}'.replace('/', '_')
    assert description['soc'] == soc
    assert description['full_name'] == f'{name} board'
    assert description['directories'] == [f'zephyr/boards/{name}']


def test_board_description_revision(describer):
    description = describer._board_description(
        'mmp@1/soc1', build_info('mmp', 'soc1', '0.7.0')['board']
    )
    assert description['revision'] == {
        'format': 'major.minor.patch',
        'default': '0.7.0',
        'requested': '1',
        'active': '0.7.0',
    }


def test_devicetree_description(describer, tmp_path):
    bindings = tmp_path / 'dts' / 'bindings'
    bindings.mkdir(parents=True)
    (bindings / 'vendor-prefixes.txt').write_text('acme\tACME\n')

    description = describer._devicetree_description(
        {
            'files': ['/workspace/zephyr/boards/plank/plank.dts', '/workspace/plank_1_0_0.overlay'],
            'include-files': ['/workspace/zephyr/boards/plank/plank.dts', '/workspace/soc.dtsi'],
            'include-dirs': ['/workspace/zephyr/dts'],
            'bindings-dirs': [str(bindings), '/nonexistent/dts/bindings'],
            'extra-dtc-flags': ['-Wno-simple_bus_reg'],
        }
    )
    assert description['source'] == 'zephyr/boards/plank/plank.dts'
    assert description['overlays'] == ['plank_1_0_0.overlay']
    assert description['files'] == ['zephyr/boards/plank/plank.dts', 'soc.dtsi']
    assert description['include_dirs'] == ['zephyr/dts']
    assert description['extra_dtc_flags'] == ['-Wno-simple_bus_reg']
    # Only the vendor prefix files that exist, and only under a bindings
    # directory that has one.
    assert description['vendor_prefixes'] == [f'{bindings}/vendor-prefixes.txt']


def test_devicetree_description_defaults(describer):
    description = describer._devicetree_description({})
    assert description['source'] is None
    assert description['overlays'] == []
    assert description['extra_dtc_flags'] == []


@pytest.mark.parametrize(
    'name, qualifiers, revision, stem',
    [
        ('plank', 'soc1', '', 'plank_soc1'),
        ('plank', 'soc1/ns', '', 'plank_soc1_ns'),
        ('mmp', 'soc1', '0.7.0', 'mmp_soc1_0_7_0'),
        ('mmp', 'soc1', 'A', 'mmp_soc1_A'),
    ],
)
def test_file_stem(describer, name, qualifiers, revision, stem):
    description = {
        'board': describer._board_description(name, build_info(name, qualifiers, revision)['board'])
    }
    assert iut.file_stem(description) == stem


def test_all_targets(describer):
    assert describer.all_targets({'plank': BOARDS['plank']}) == ['plank/soc1', 'plank/soc1/ns']


def test_rel(describer):
    assert describer._rel('/workspace/zephyr/dts') == 'zephyr/dts'
    assert describer._rel('/elsewhere/dts') == '/elsewhere/dts'


def test_no_preprocessor_by_default(monkeypatch, tmp_path):
    """Without --preprocessor, CMake is left to find one."""
    monkeypatch.setattr(iut.shutil, 'which', lambda name: f'/usr/bin/{name}')
    describer = iut.BoardDescriber(BOARDS)

    captured = []
    monkeypatch.setattr(
        iut.subprocess,
        'run',
        lambda cmd, **kwargs: captured.append(cmd) or iut.subprocess.CompletedProcess(cmd, 0),
    )
    describer._run_cmake('plank/soc1', tmp_path)
    assert not any(a.startswith('-DCMAKE_DTS_PREPROCESSOR') for a in captured[0])
