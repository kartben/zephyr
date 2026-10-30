# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

'''Tests for how "west board-modules" finds the west projects a board target needs.'''

import argparse
import configparser
import os
import shutil
import subprocess
import textwrap
from argparse import Namespace
from collections import defaultdict

import pytest
from conftest import _FakeConfig

import board_modules
from board_modules import (
    BoardModules,
    Project,
    kconfig_selects,
    needed_modules,
    peek_module_name,
    project_filter_with,
    resolve_modules,
    resolve_target,
    undeprecate,
)

# A SoC root and a board root with one board for each case
FILES = {
    'soc/acme/soc.yml': '''\
        family:
          - name: acme
            modules: [hal_acme]
            series:
              - name: one
                socs:
                  - name: soc1
                  - name: soc2
                    cpuclusters:
                      - name: cpuapp
                        modules: [hal_app]
                      - name: cpunet
                  - name: soc3
                  - name: soc4
        socs:
          - name: soc5
          - name: soc6
        ''',
    'soc/acme/Kconfig': '''\
        config SOC_PART_ONE
            select SOC_SOC1

        config SOC_SOC1
            select ARM
            select CPU_CORTEX_M4

        config SOC_SOC2_APP
            select ARM
            select CPU_CORTEX_M33 if !SOMETHING

        config SOC_SOC2_NET
            select RISCV

        config SOC_SOC3
            select CPU_CORTEX_R5
        ''',
    # Single SoC, revisions, a variant, a twister file by short name
    'boards/acme/single/board.yml': '''\
        board:
          name: single
          full_name: Single
          vendor: acme
          revision:
            format: major.minor.patch
            default: "2.0.0"
            revisions:
              - name: "1.0.0"
              - name: "2.0.0"
          socs:
            - name: soc1
              variants:
                - name: xip
        ''',
    'boards/acme/single/single.yaml': 'identifier: single\narch: arm\n',
    'boards/acme/single/Kconfig.single': 'config BOARD_SINGLE\n    select SOC_PART_ONE\n',
    # CPU clusters, a non-secure variant, no twister file for cpunet
    'boards/acme/multi/board.yml': '''\
        board:
          name: multi
          full_name: Multi
          vendor: acme
          socs:
            - name: soc2
              variants:
                - name: ns
                  cpucluster: cpuapp
        ''',
    'boards/acme/multi/multi_soc2_cpuapp.yaml': 'identifier: multi/soc2/cpuapp\narch: arm\n',
    'boards/acme/multi/multi_soc2_cpuapp_ns.yaml': 'identifier: multi/soc2/cpuapp/ns\narch: arm\n',
    'boards/acme/multi/Kconfig.multi': '''\
        config BOARD_MULTI
            select SOC_SOC2_APP if BOARD_MULTI_SOC2_CPUAPP || \\
                BOARD_MULTI_SOC2_CPUAPP_NS
            select SOC_SOC2_NET if BOARD_MULTI_SOC2_CPUNET
        ''',
    # Cortex-R, through the SoC symbol named after the SoC
    'boards/acme/rboard/board.yml': (
        'board:\n  name: rboard\n  full_name: R\n  vendor: acme\n  socs:\n    - name: soc3\n'
    ),
    'boards/acme/rboard/rboard.yaml': 'identifier: rboard\narch: arm\n',
    # twister.yaml, with a variant that overrides the architecture
    'boards/acme/dsp/board.yml': (
        'board:\n  name: dsp\n  full_name: DSP\n  vendor: acme\n  socs:\n    - name: soc4\n'
    ),
    'boards/acme/dsp/twister.yaml': 'arch: arm\nvariants:\n  dsp/soc4:\n    arch: xtensa\n',
    # Nothing tells the architecture
    'boards/acme/mystery/board.yml': (
        'board:\n  name: mystery\n  full_name: M\n  vendor: acme\n  socs:\n    - name: soc5\n'
    ),
    # 64-bit non-secure target, in a twister file named with "//"
    'boards/acme/bigcore/board.yml': '''\
        board:
          name: bigcore
          full_name: Big
          vendor: acme
          socs:
            - name: soc6
              variants:
                - name: ns
        ''',
    'boards/acme/bigcore/bigcore_soc6_ns.yaml': 'identifier: bigcore//ns\narch: arm64\n',
}


@pytest.fixture
def hardware(tmp_path):
    for path, text in FILES.items():
        file = tmp_path / path
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(textwrap.dedent(text))
    roots = Namespace(
        arch_roots=[tmp_path],
        board_roots=[tmp_path],
        soc_roots=[tmp_path],
        board=None,
        board_dir=[],
    )
    boards = board_modules.list_boards.find_v2_boards(roots)
    socs = {soc.name: soc for soc in board_modules.list_hardware.find_v2_systems(roots).get_socs()}
    return boards, socs, [tmp_path]


@pytest.mark.parametrize(
    'spec, names',
    [
        ('single', ['single/soc1']),
        ('single@1.0.0', ['single@1.0.0/soc1']),
        ('single//xip', ['single/soc1/xip']),
        ('single/soc1/xip', ['single/soc1/xip']),
        ('multi', ['multi/soc2/cpuapp', 'multi/soc2/cpuapp/ns', 'multi/soc2/cpunet']),
        ('multi/soc2/cpuapp', ['multi/soc2/cpuapp']),
        ('multi/soc2/cpuapp/ns', ['multi/soc2/cpuapp/ns']),
    ],
)
def test_board_target_names_targets(hardware, spec, names):
    boards, socs, _ = hardware
    assert [t.name for t in resolve_target(spec, boards, socs)] == names


def one_target(hardware, spec):
    boards, socs, _ = hardware
    targets = resolve_target(spec, boards, socs)
    assert len(targets) == 1
    return targets[0]


def test_qualifiers_give_the_soc_cluster_and_variants(hardware):
    target = one_target(hardware, 'multi/soc2/cpuapp/ns')
    assert (target.soc.name, target.cpucluster, target.variants) == ('soc2', 'cpuapp', ['ns'])
    target = one_target(hardware, 'single//xip')
    assert (target.soc.name, target.cpucluster, target.variants) == ('soc1', None, ['xip'])


@pytest.mark.parametrize(
    'spec, error',
    [
        ('singel', 'no board named "singel"; did you mean single'),
        ('multi@2', 'board "multi" has no revisions'),
        ('multi/soc2/cpux', 'is not a board target; the targets of multi are: multi/soc2/cpuapp'),
        ('multi@1@2', 'invalid board target'),
    ],
)
def test_bad_board_targets(hardware, spec, error):
    boards, socs, _ = hardware
    with pytest.raises(ValueError, match=error):
        resolve_target(spec, boards, socs)


@pytest.mark.parametrize(
    'spec, needs',
    [
        ('single', {'hal_acme': 'SoC soc1', 'cmsis_6': 'Cortex-M CPU'}),
        # No twister file: the architecture comes from Kconfig
        ('single//xip', {'hal_acme': 'SoC soc1', 'cmsis_6': 'Cortex-M CPU'}),
        (
            'multi/soc2/cpuapp',
            {
                'hal_acme': 'SoC soc2',
                'hal_app': 'CPU cluster cpuapp of SoC soc2',
                'cmsis_6': 'Cortex-M CPU',
            },
        ),
        (
            'multi/soc2/cpuapp/ns',
            {
                'hal_acme': 'SoC soc2',
                'hal_app': 'CPU cluster cpuapp of SoC soc2',
                'cmsis_6': 'Cortex-M CPU',
                'trusted-firmware-m': 'non-secure target',
            },
        ),
        ('multi/soc2/cpunet', {'hal_acme': 'SoC soc2'}),
        ('rboard', {'hal_acme': 'SoC soc3', 'cmsis': 'Cortex-A/R CPU'}),
        ('dsp', {'hal_acme': 'SoC soc4', 'xtensa': 'Xtensa CPU'}),
        (
            'mystery',
            {
                'cmsis_6': 'unknown architecture',
                'cmsis': 'unknown architecture',
                'xtensa': 'unknown architecture',
            },
        ),
        ('bigcore//ns', {'trusted-firmware-a': 'non-secure target'}),
    ],
)
def test_needed_modules(hardware, spec, needs):
    _, _, roots = hardware
    assert needed_modules(one_target(hardware, spec), roots) == needs


def test_kconfig_selects_keep_their_condition(tmp_path):
    kconfig = tmp_path / 'Kconfig'
    kconfig.write_text(
        'config A\n  select B if C || \\\n    D\n  select E\n\nsource "x"\n  select F\n'
    )
    selects = kconfig_selects([kconfig])
    assert [(s, c.split() if c else c) for s, c in selects['A']] == [
        ('B', ['C', '||', 'D']),
        ('E', None),
    ]
    assert list(selects) == ['A']


PROJECTS = [
    Project('hal_acme', 'modules/hal/acme'),
    Project('hal_other', 'modules/hal/other'),
    Project('zephyr-thing', 'modules/lib/zephyr-thing'),
    Project('renamed', 'modules/hal/renamed', cloned=True, module='hal_renamed'),
    Project('twin1', 'modules/a/twin'),
    Project('twin2', 'modules/b/twin'),
]


def test_modules_map_to_projects_by_module_yml_then_name_then_path():
    found, missing = resolve_modules(
        ['hal_acme', 'other', 'hal_renamed', 'renamed', 'thing', 'twin'], PROJECTS
    )
    assert {m: p.name for m, p in found.items()} == {
        'hal_acme': 'hal_acme',
        'other': 'hal_other',
        'hal_renamed': 'renamed',
    }
    # A cloned project only provides the module of its zephyr/module.yml
    assert missing == ['renamed', 'thing', 'twin']


def test_peek_reads_only_the_projects_that_may_provide_a_module():
    peeked = []

    def peek(project):
        peeked.append(project.name)
        return {'zephyr-thing': 'thing', 'twin2': 'twin'}.get(project.name)

    found, missing = resolve_modules(['hal_acme', 'thing', 'twin', 'nothing'], PROJECTS, peek)
    assert {m: p.name for m, p in found.items()} == {
        'hal_acme': 'hal_acme',
        'thing': 'zephyr-thing',
        'twin': 'twin2',
    }
    assert missing == ['nothing']
    assert peeked == ['zephyr-thing', 'twin1', 'twin2']


def test_modules_bring_the_modules_they_depend_on():
    needs = defaultdict(list, {'sim': ['SoC soc1']})
    projects = [
        Project('sim_models', 'modules/sim', cloned=True, module='sim', depends=['hal_acme']),
        *PROJECTS,
    ]
    found = BoardModules().resolve(needs, projects)
    assert {m: p.name for m, p in found.items()} == {'sim': 'sim_models', 'hal_acme': 'hal_acme'}
    assert needs['hal_acme'] == ['needed by module sim']


def test_projects_are_listed_with_their_modules_and_reasons(capsys):
    needs = {'cmsis_6': ['Cortex-M CPU'], 'other': ['SoC soc1', 'SoC soc2']}
    found = {'cmsis_6': Project('cmsis_6', 'modules/hal/cmsis_6'), 'other': PROJECTS[1]}
    command = BoardModules()
    command.config = _FakeConfig()
    command.print_projects(found, needs, names_only=False)
    command.print_projects(found, needs, names_only=True)
    assert capsys.readouterr().out.splitlines() == [
        'cmsis_6    cmsis_6  Cortex-M CPU',
        'hal_other  other    SoC soc1, SoC soc2',
        'cmsis_6',
        'hal_other',
    ]


ZEPHYR_WEST_YML = '''\
manifest:
  remotes:
    - name: upstream
      url-base: https://example.com
  defaults:
    remote: upstream
  projects:
    - name: cmsis_6
      revision: c1
      path: modules/hal/cmsis_6
    - name: hal_atmel
      revision: a1
      path: modules/hal/atmel
    - name: zephyr-thing
      revision: t1
      path: modules/lib/zephyr-thing
  self:
    path: zephyr
    import: submanifests
'''


class Config(_FakeConfig):
    def set(self, option, value):
        self._values[option] = value

    def items(self, configfile=None):
        return self._values.items()


class FakeManifest:
    '''The parts of a west manifest that BoardModules reads; the first project is the
    manifest project.'''

    relative_path = 'app/west.yml'

    def __init__(self, topdir, *names_and_paths):
        self.projects = [
            Namespace(
                name=name,
                path=path,
                abspath=os.fspath(topdir / path),
                url=f'https://example.com/{name}',
                revision='main',
                is_cloned=lambda: False,
            )
            for name, path in names_and_paths
        ]

    def is_active(self, _):
        return True


@pytest.fixture
def workspace(tmp_path, monkeypatch):
    '''Returns a command for a workspace with zephyr/west.yml, but no manifest yet.'''
    (tmp_path / 'zephyr').mkdir()
    (tmp_path / 'zephyr' / 'west.yml').write_text(ZEPHYR_WEST_YML)
    monkeypatch.setattr(board_modules, 'ZEPHYR_BASE', tmp_path / 'zephyr')
    command = BoardModules()
    command.topdir = os.fspath(tmp_path)
    command.config = Config({'manifest.path': 'app', 'manifest.file': 'west.yml'})
    return command


def test_projects_that_an_import_of_zephyr_leaves_out_still_resolve(
    workspace, tmp_path, monkeypatch, capsys
):
    # As with "import: name-allowlist: [cmsis_6]"
    workspace.manifest = FakeManifest(
        tmp_path, ('manifest', 'app'), ('zephyr', 'zephyr'), ('cmsis_6', 'modules/hal/cmsis_6')
    )
    monkeypatch.setattr(board_modules, 'peek_module_name', lambda p: p.name.split('-')[-1])
    needs = defaultdict(list, {'cmsis_6': ['Cortex-M CPU'], 'atmel': ['SoC x'], 'thing': ['SoC y']})
    found = workspace.resolve(needs, workspace.projects())
    # By name, by path and from the remote
    assert {m: (p.name, p.in_manifest, p.revision) for m, p in found.items()} == {
        'cmsis_6': ('cmsis_6', True, 'main'),
        'atmel': ('hal_atmel', False, 'a1'),
        'thing': ('zephyr-thing', False, 't1'),
    }
    capsys.readouterr()

    workspace.check_projects(found.values(), Namespace(update=False, filter=False))
    message = 'app/west.yml leaves out these projects of zephyr/west.yml: hal_atmel zephyr-thing\n'
    assert message in capsys.readouterr().err
    with pytest.raises(SystemExit):
        workspace.check_projects(found.values(), Namespace(update=True, filter=False))
    workspace.print_projects(found, needs, allowlist=True)
    assert capsys.readouterr().out.splitlines() == ['- cmsis_6', '- hal_atmel', '- zephyr-thing']


@pytest.mark.parametrize(
    'first, value',
    [
        # The manifest imports zephyr, which must stay active for that
        ([('manifest', 'app'), ('zephyr', 'zephyr')], '-.*,+zephyr,+cmsis_6,+hal_atmel'),
        # Zephyr is the manifest project
        ([('zephyr', 'zephyr')], '-.*,+cmsis_6,+hal_atmel'),
    ],
)
def test_update_filters_a_plain_west_update(workspace, tmp_path, monkeypatch, first, value):
    workspace.manifest = FakeManifest(
        tmp_path, *first, ('cmsis_6', 'modules/hal/cmsis_6'), ('hal_atmel', 'modules/hal/atmel')
    )
    runs = []

    def run(command, cwd, env):
        config = configparser.ConfigParser()
        config.read(env['WEST_CONFIG_LOCAL'])
        runs.append((command[1:], cwd, {s: dict(config[s]) for s in config.sections()}))
        return subprocess.CompletedProcess(command, 0)

    monkeypatch.setattr(board_modules.subprocess, 'run', run)
    # West refuses "west update cmsis_6 hal_atmel" for projects that come from an import
    workspace.west_update(['cmsis_6', 'hal_atmel'])
    local = {'manifest': {'path': 'app', 'file': 'west.yml', 'project-filter': value}}
    assert runs == [(['-m', 'west', 'update'], os.fspath(tmp_path), local)]
    # The local configuration itself is left alone
    assert workspace.config.get('manifest.project-filter') is None

    workspace.add_to_filter(['cmsis_6', 'hal_atmel'])
    assert workspace.config.get('manifest.project-filter') == value


@pytest.mark.skipif(shutil.which('git') is None, reason='needs git')
def test_peek_reads_module_yml_from_the_remote(tmp_path):
    remote = tmp_path / 'remote'
    (remote / 'zephyr').mkdir(parents=True)
    (remote / 'zephyr' / 'module.yml').write_text('name: thing\n')
    git = ['git', '-C', str(remote), '-c', 'user.name=Test', '-c', 'user.email=test@example.com']
    subprocess.run(['git', 'init', '-q', str(remote)], check=True)
    subprocess.run(git + ['add', '.'], check=True)
    subprocess.run(git + ['commit', '-q', '-m', 'module'], check=True)

    project = Project('zephyr-thing', 'modules/lib/zephyr-thing', url=str(remote), revision='HEAD')
    assert peek_module_name(project) == 'thing'
    missing = Project('gone', 'modules/lib/gone', url=str(tmp_path / 'gone'), revision='HEAD')
    assert peek_module_name(missing) is None


@pytest.mark.parametrize(
    'current, names, value',
    [
        (None, ['b', 'a'], '-.*,+b,+a'),
        ('', ['a'], '-.*,+a'),
        ('-.*,+a', ['a', 'b'], '-.*,+a,+b'),
        ('+zephyr-lang-rust', ['a'], '-.*,+zephyr-lang-rust,+a'),
        ('-.*,+a,-a', ['a'], '-.*,+a,-a,+a'),
        ('-.*,+hal_.*', ['hal_x', 'c.d'], r'-.*,+hal_.*,+c\.d'),
    ],
)
def test_project_filter_adds_what_it_does_not_activate(current, names, value):
    assert project_filter_with(current, names) == value


@pytest.mark.parametrize(
    'spec, replacement',
    [
        ('old', 'new/soc1'),
        ('old@1.0', 'new@1.0/soc1'),
        ('old/a/b', 'newer/a/b'),
        ('kept', 'kept'),
        ('kept@1/a', 'kept@1/a'),
    ],
)
def test_deprecated_board_names_are_replaced(spec, replacement):
    deprecated = {'old': 'new/soc1', 'old/a/b': 'newer/a/b'}
    assert undeprecate(spec, deprecated) == replacement


def test_quiet_option_leaves_the_verbosity_of_west_alone():
    # West subtracts the "quiet" count of its own parser from the verbosity
    parser = argparse.ArgumentParser(allow_abbrev=False)
    parser.add_argument('-q', '--quiet', default=0, action='count')
    BoardModules().do_add_parser(parser.add_subparsers())
    args = parser.parse_args(['board-modules', '-q', 'single', 'multi'])
    assert (args.quiet, args.names_only, args.targets) == (0, True, ['single', 'multi'])
