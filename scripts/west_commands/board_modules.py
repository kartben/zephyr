# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

'''The "west board-modules" command: the west projects that board targets need.'''

import argparse
import configparser
import difflib
import functools
import os
import re
import subprocess
import sys
import tempfile
import textwrap
from argparse import Namespace
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath

import yaml
from west.commands import Verbosity, WestCommand
from west.configuration import ConfigFile
from west.manifest import ImportFlag, MalformedManifest, Manifest

from zephyr_ext_common import ZEPHYR_BASE

sys.path.append(os.fspath(Path(__file__).parent.parent))
import list_boards
import list_hardware
import zephyr_module

try:
    from yaml import CSafeLoader as SafeLoader
except ImportError:
    from yaml import SafeLoader

# <board>[@<revision>][/<qualifiers>], as boards.cmake parses BOARD
BOARD_TARGET_RE = re.compile(r'([^@/]+)(?:@([^@/]+))?(?:/([^@]+))?')

# Modules that a build needs because of its CPU, which no soc.yml lists
CMSIS_6 = 'cmsis_6'  # Cortex-M
CMSIS = 'cmsis'  # Cortex-A and Cortex-R
XTENSA = 'xtensa'  # Xtensa, unless built with the xcc or xt-clang toolchains

# Secure firmware of non-secure ("ns") board targets, by architecture
SECURE_FIRMWARE = {'arm': 'trusted-firmware-m', 'arm64': 'trusted-firmware-a'}

KCONFIG_ENTRY_RE = re.compile(r'\s*(?:menu)?config\s+(\w+)\s*$')
KCONFIG_SELECT_RE = re.compile(r'\s+select\s+(\w+)(?:\s+if\s+(.+))?')
KCONFIG_OTHER_RE = re.compile(
    r'\s*(?:if|endif|menu|endmenu|choice|endchoice|comment|source|rsource|osource|orsource)\b'
)
CPU_PROFILE_RE = re.compile(r'CPU_(?:AARCH32_)?CORTEX_([MAR])')
ARCH_SYMBOLS = {
    'ARC': 'arc',
    'ARCH_POSIX': 'posix',
    'ARM': 'arm',
    'ARM64': 'arm64',
    'HEXAGON': 'hexagon',
    'MIPS': 'mips',
    'OPENRISC': 'openrisc',
    'RISCV': 'riscv',
    'RX': 'rx',
    'SPARC': 'sparc',
    'TRICORE': 'tricore',
    'X86': 'x86',
    'XTENSA': 'xtensa',
}


@dataclass
class Target:
    '''A board target and the SoC, CPU cluster and variants its qualifiers name.'''

    name: str
    board: list_boards.Board
    qualifiers: str
    soc: list_hardware.Soc | None = None
    cpucluster: str | None = None
    variants: list[str] = field(default_factory=list)


@dataclass
class Project:
    '''A west project, and the module its zephyr/module.yml names once it is cloned.'''

    name: str
    path: str
    url: str = ''
    revision: str = ''
    cloned: bool = False
    active: bool = True
    module: str | None = None
    depends: list[str] = field(default_factory=list)
    # False for a project of zephyr/west.yml that the import of zephyr leaves out
    in_manifest: bool = True


def default_qualifiers(board, qualifiers):
    '''Returns the qualifiers, starting with the SoC of a board that has only one.

    The build system does the same, so "<board>" and "<board>//<variant>" work for
    such boards.
    '''
    if len(board.socs) != 1:
        return qualifiers
    soc = board.socs[0].name
    if qualifiers is None:
        return soc
    if qualifiers.startswith('/'):
        return soc + qualifiers
    return qualifiers


def undeprecate(spec, deprecated):
    '''Returns the board target that replaces spec in boards/deprecated.cmake, or spec.'''
    match = BOARD_TARGET_RE.fullmatch(spec)
    if match is None:
        return spec
    name, revision, qualifiers = match.groups()
    replacement = deprecated.get(name if qualifiers is None else f'{name}/{qualifiers}')
    if replacement is None or revision is None:
        return replacement or spec
    board, _, qualifiers = replacement.partition('/')
    return f'{board}@{revision}/{qualifiers}' if qualifiers else f'{board}@{revision}'


def resolve_target(spec, boards, socs):
    '''Returns the board targets that spec names.

    A complete board target names itself. Qualifiers that only start board targets
    name all of them: a board name alone names every target of a board with several
    SoCs or CPU clusters.
    '''
    match = BOARD_TARGET_RE.fullmatch(spec)
    if match is None:
        raise ValueError(
            f'invalid board target "{spec}", expected <board>[@<revision>][/<qualifiers>]'
        )
    name, revision, qualifiers = match.groups()
    board = boards.get(name)
    if board is None:
        close = difflib.get_close_matches(name, boards)
        hint = f'; did you mean {", ".join(close)}?' if close else ''
        raise ValueError(f'no board named "{name}"{hint}')
    if revision is not None and board.revision_format is None:
        raise ValueError(f'board "{name}" has no revisions')

    qualifiers = default_qualifiers(board, qualifiers)
    valid = list_boards.board_v2_qualifiers(board)
    if qualifiers in valid:
        selected = [qualifiers]
    else:
        prefix = '' if qualifiers is None else qualifiers + '/'
        selected = [q for q in valid if q.startswith(prefix)]
    if not selected:
        raise ValueError(
            f'"{spec}" is not a board target; the targets of {name} are: '
            + ', '.join(f'{name}/{q}' for q in valid)
        )

    board_name = name if revision is None else f'{name}@{revision}'
    targets = []
    for q in selected:
        soc, *variants = q.split('/')
        target = Target(f'{board_name}/{q}', board, q, socs.get(soc), variants=variants)
        if target.soc is not None and variants and variants[0] in target.soc.cpuclusters:
            target.cpucluster = variants.pop(0)
        targets.append(target)
    return targets


def load_yaml(path):
    try:
        with open(path, encoding='utf-8') as f:
            return yaml.load(f, Loader=SafeLoader)
    except (OSError, yaml.YAMLError):
        return None


def twister_arch(target):
    '''Returns the architecture that the twister board file of target gives, or None.'''
    board = target.board

    def names_target(identifier):
        match = BOARD_TARGET_RE.fullmatch(str(identifier))
        return (
            match is not None
            and match.group(1) == board.name
            and default_qualifiers(board, match.group(3)) == target.qualifiers
        )

    arch = None
    for directory in board.directories:
        data = load_yaml(Path(directory) / 'twister.yaml')
        if not isinstance(data, dict):
            continue
        for identifier, variant in (data.get('variants') or {}).items():
            if names_target(identifier) and isinstance(variant, dict) and variant.get('arch'):
                return variant['arch']
        if Path(directory) == Path(board.dir):
            arch = data.get('arch')
    if arch is not None:
        return arch

    for directory in board.directories:
        for path in sorted(Path(directory).glob('*.yaml')):
            data = load_yaml(path) if path.name != 'twister.yaml' else None
            if isinstance(data, dict) and names_target(data.get('identifier')):
                return data.get('arch')
    return None


def kconfig_name(*names):
    return '_'.join(re.sub(r'\W', '_', name).upper() for name in names)


def kconfig_selects(paths):
    '''Maps the symbols that Kconfig files define to the symbols they select, each
    with the condition of the select, or None.
    '''
    selects = defaultdict(list)
    for path in paths:
        text = Path(path).read_text(encoding='utf-8', errors='replace').replace('\\\n', ' ')
        symbol = None
        for line in text.splitlines():
            if match := KCONFIG_ENTRY_RE.match(line):
                symbol = match.group(1)
            elif KCONFIG_OTHER_RE.match(line):
                symbol = None
            elif symbol is not None and (match := KCONFIG_SELECT_RE.match(line)):
                selects[symbol].append((match.group(1), match.group(2)))
    return selects


@functools.cache
def soc_selects(soc_roots):
    '''Returns kconfig_selects() for the Kconfig files of the SoCs in soc_roots.'''
    paths = []
    for root in soc_roots:
        paths.extend(sorted(p for p in (Path(root) / 'soc').rglob('Kconfig*') if p.is_file()))
    return kconfig_selects(paths)


def kconfig_symbols(target, soc_roots):
    '''Returns the Kconfig symbols that a build for target selects, as far as the
    Kconfig files of its board and of the SoCs tell.

    The selects of the board are those for target, but the selects of the SoCs
    are followed whatever their condition.
    '''
    board = target.board
    board_symbol = kconfig_name('BOARD', board.name)
    target_symbol = kconfig_name('BOARD', board.name, *target.qualifiers.split('/'))
    todo = []
    for directory in board.directories:
        path = Path(directory) / f'Kconfig.{board.name}'
        if not path.is_file():
            continue
        for selected, condition in kconfig_selects([path]).get(board_symbol, []):
            if condition is None or target_symbol in re.findall(r'\w+', condition):
                todo.append(selected)
    # Also by name, for boards that select them under other conditions
    soc = target.soc
    if soc is not None:
        todo.append(kconfig_name('SOC', soc.name))
        if target.cpucluster:
            todo.append(kconfig_name('SOC', soc.name, target.cpucluster))
        if soc.series:
            todo.append(kconfig_name('SOC_SERIES', soc.series))
        if soc.family:
            todo.append(kconfig_name('SOC_FAMILY', soc.family))

    selects = soc_selects(tuple(dict.fromkeys(Path(root).resolve() for root in soc_roots)))
    reached = set()
    while todo:
        symbol = todo.pop()
        if symbol not in reached:
            reached.add(symbol)
            todo.extend(selected for selected, _ in selects.get(symbol, ()))
    return reached


def cpu_modules(archs, profiles):
    '''Returns the modules, each with the reason, that CPUs of the architectures need.

    profiles are the ARM profiles, among "m", "a" and "r", of the ARM CPUs.
    '''
    if not archs:
        return [(module, 'unknown architecture') for module in (CMSIS_6, CMSIS, XTENSA)]
    modules = []
    if 'arm' in archs:
        if not profiles:
            modules += [
                (CMSIS_6, 'ARM CPU of unknown profile'),
                (CMSIS, 'ARM CPU of unknown profile'),
            ]
        if 'm' in profiles:
            modules.append((CMSIS_6, 'Cortex-M CPU'))
        if profiles & {'a', 'r'}:
            modules.append((CMSIS, 'Cortex-A/R CPU'))
    if 'xtensa' in archs:
        modules.append((XTENSA, 'Xtensa CPU'))
    return modules


def needed_modules(target, soc_roots):
    '''Returns the modules that a build for target needs, each with the reason.

    The architecture comes from the twister board file of target, or else from
    the Kconfig files of its board and of the SoCs, which also give the profile
    of ARM CPUs.
    '''
    needs = {}
    soc = target.soc
    if soc is not None:
        for module in soc.modules:
            needs.setdefault(module, f'SoC {soc.name}')
        for module in soc.cpucluster_modules.get(target.cpucluster, []):
            needs.setdefault(module, f'CPU cluster {target.cpucluster} of SoC {soc.name}')

    symbols = kconfig_symbols(target, soc_roots)
    arch = twister_arch(target)
    archs = {arch} if arch else {ARCH_SYMBOLS[s] for s in symbols if s in ARCH_SYMBOLS}
    profiles = {m.group(1).lower() for s in symbols if (m := CPU_PROFILE_RE.match(s))}
    for module, reason in cpu_modules(archs, profiles):
        needs.setdefault(module, reason)

    if 'ns' in target.variants:
        for arch in sorted(archs & SECURE_FIRMWARE.keys()):
            needs.setdefault(SECURE_FIRMWARE[arch], 'non-secure target')
    return needs


def resolve_modules(names, projects, peek=None):
    '''Maps module names to the projects that provide them.

    A cloned project provides the module that its zephyr/module.yml names. A
    project that is not cloned yet is expected to provide the module named like
    the project, or else like the last component of its path. For a module that
    neither matches, peek, if given, is called with the projects whose name or
    path ends with the module name, and returns the module that the remote
    zephyr/module.yml of a project names.

    Returns the projects by module name, and the modules that no project provides.
    '''
    provided = {p.module: p for p in projects if p.cloned and p.module is not None}
    uncloned = [p for p in projects if not p.cloned]
    found, missing = {}, []
    for name in names:
        if name in provided:
            found[name] = provided[name]
            continue
        guesses = [p for p in uncloned if p.name == name] or [
            p for p in uncloned if PurePosixPath(p.path).name == name
        ]
        if len(guesses) == 1:
            found[name] = guesses[0]
            continue
        if peek is not None:
            candidates = guesses or [
                p
                for p in uncloned
                if p.name.endswith(name) or PurePosixPath(p.path).name.endswith(name)
            ]
            project = next((p for p in candidates if peek(p) == name), None)
            if project is not None:
                found[name] = project
                continue
        missing.append(name)
    return found, missing


def peek_module_name(project):
    '''Returns the module that the zephyr/module.yml of a project not cloned yet names.

    The file is read from the remote, which only sends the commit, then the trees
    and blob that lead to it. Returns None if it cannot be read.
    '''
    env = dict(os.environ, GIT_TERMINAL_PROMPT='0')
    with tempfile.TemporaryDirectory(prefix='west-', ignore_cleanup_errors=True) as tmp:

        def git(*args, check=True):
            return subprocess.run(
                ['git', '-C', tmp, *args],
                check=check,
                capture_output=True,
                text=True,
                env=env,
                timeout=300,
            )

        try:
            git('init', '-q', '--bare')
            git('fetch', '-q', '--depth=1', '--filter=tree:0', project.url, project.revision)
            for name in ('module.yml', 'module.yaml'):
                show = git('cat-file', '-p', f'FETCH_HEAD:zephyr/{name}', check=False)
                if show.returncode == 0:
                    meta = yaml.load(show.stdout, Loader=SafeLoader)
                    default = PurePosixPath(project.path).name
                    return meta.get('name', default) if isinstance(meta, dict) else default
        except (OSError, subprocess.SubprocessError, yaml.YAMLError):
            pass
    return None


def manifest_file_projects(path):
    '''Returns the projects of a manifest file, but those of its imports.'''
    try:
        data = Path(path).read_text(encoding='utf-8')
        return Manifest.from_data(data, import_flags=ImportFlag.IGNORE).projects[1:]
    except (OSError, MalformedManifest):
        return []


def project_filter_with(current, names):
    '''Returns a manifest.project-filter value that makes only the projects
    that current makes active, and names, active.

    Elements are added to the current value, which is kept as it is otherwise.
    '''
    elements = [e.strip() for e in (current or '').split(',') if e.strip()]
    if elements[:1] != ['-.*']:
        elements.insert(0, '-.*')
    for name in names:
        active = None
        for element in elements:
            try:
                if re.fullmatch(element[1:], name):
                    active = element.startswith('+')
            except re.error:
                pass
        if not active:
            elements.append('+' + (name if re.fullmatch(r'[\w-]+', name) else re.escape(name)))
    return ','.join(elements)


class BoardModules(WestCommand):
    def __init__(self):
        super().__init__(
            'board-modules',
            # Keep this in sync with the string in west-commands.yml.
            'list or fetch the west projects a board target needs',
            textwrap.dedent('''\
            List the west projects that board targets need, to fetch those
            instead of every project of the manifest.

            A board target needs the projects of the Zephyr modules that
            soc.yml lists for its SoC and CPU cluster, of the CMSIS or Xtensa
            module that its CPU needs, and of TF-M or TF-A for a non-secure
            ("ns") target. Modules that applications, board configurations or
            snippets enable are not listed.'''),
            accepts_unknown_args=False,
        )
        self.peeked = {}

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=self.description,
            epilog=textwrap.dedent('''\
            Each line names a project, the modules it provides and why the
            targets need them. Before a project is cloned, the module it
            provides is not known: a module is taken to be in the project of
            the same name, or else in the one whose path ends with its name.
            If neither exists, zephyr/module.yml is read from the remote of
            the projects whose name or path ends with the module name.

            Without --update or --filter, nothing changes. "west update"
            updates the projects that -q prints when given their names, and
            every active project when given none, but refuses the names of
            projects that come from an import, as when the manifest imports
            zephyr. --update runs it with a manifest.project-filter that
            activates only the projects, and zephyr if the manifest imports
            it, which west then updates as well.

            The build ignores inactive projects, cloned or not. --update
            refuses to fetch inactive projects, unless --filter activates them.
            It also refuses projects of zephyr/west.yml that the manifest
            leaves out, for example with the name-allowlist of its import of
            zephyr: --allowlist prints the entries of such a list.
            '''),
        )

        output = parser.add_mutually_exclusive_group()
        # Not dest='quiet', which would lower the verbosity west sets from its own -q
        output.add_argument(
            '-q',
            '--quiet',
            dest='names_only',
            action='store_true',
            help='print only the names of the projects',
        )
        output.add_argument(
            '--allowlist',
            action='store_true',
            help='print the names of the projects as a YAML list, for the name-allowlist '
            'of an import of zephyr',
        )
        parser.add_argument(
            '--update',
            action='store_true',
            help='run "west update" on the projects, then on the projects of the modules '
            'they depend on',
        )
        parser.add_argument(
            '--filter',
            action='store_true',
            help='add the projects, and zephyr if the manifest imports it, to '
            'manifest.project-filter in the local configuration, which makes the projects '
            'it does not activate inactive, so that "west update" only updates them and '
            'those it already activates',
        )
        parser.add_argument(
            '--board-root',
            dest='board_roots',
            metavar='DIR',
            default=[],
            type=Path,
            action='append',
            help='add a board root, may be given more than once',
        )
        parser.add_argument(
            '--soc-root',
            dest='soc_roots',
            metavar='DIR',
            default=[],
            type=Path,
            action='append',
            help='add a SoC root, may be given more than once',
        )
        parser.add_argument(
            'targets',
            metavar='BOARD',
            nargs='+',
            help='board target, as given to "west build -b"; qualifiers that only start '
            'board targets select all of them',
        )
        return parser

    def do_run(self, args, _):
        boards, socs, soc_roots = self.hardware(args)
        needs = defaultdict(list)
        for spec in args.targets:
            for target in self.targets(spec, boards, socs):
                target_needs = needed_modules(target, soc_roots)
                for module, reason in target_needs.items():
                    if reason not in needs[module]:
                        needs[module].append(reason)
                if 'ns' in target.variants:
                    self.note_secure_firmware(target, target_needs)

        found = self.resolve(needs, self.projects())
        self.check_projects(found.values(), args)
        self.print_projects(found, needs, args.names_only, args.allowlist)
        names = sorted({p.name for p in found.values()})
        if args.filter:
            self.add_to_filter(names)
        if args.update:
            self.update(names, needs, args)

    def check_projects(self, projects, args):
        '''Warns about projects that the build cannot use, or dies for --update.'''
        report = self.die if args.update else self.wrn
        absent = sorted({p.name for p in projects if not p.in_manifest})
        if absent:
            report(
                f'{self.manifest.relative_path} leaves out these projects of '
                f'zephyr/west.yml: {" ".join(absent)}\n'
                '  Hint: add them to the name-allowlist of the import of zephyr; '
                '--allowlist prints all the projects as entries of that list.'
            )
        inactive = sorted({p.name for p in projects if not p.active})
        if inactive and not args.filter:
            report(
                'the build ignores inactive projects, cloned or not, and these are '
                f'inactive: {" ".join(inactive)}\n'
                '  Hint: --filter activates them in manifest.project-filter.'
            )

    def hardware(self, args):
        '''Returns the boards by name, the SoCs by name and the SoC roots.'''
        roots = {
            'arch_root': [ZEPHYR_BASE],
            'board_root': [ZEPHYR_BASE, *args.board_roots],
            'soc_root': [ZEPHYR_BASE, *args.soc_roots],
        }
        for module in zephyr_module.parse_modules(
            ZEPHYR_BASE, self.manifest, require_yaml_validation=False
        ):
            settings = module.meta.get('build', {}).get('settings', {})
            for key, paths in roots.items():
                if settings.get(key) is not None:
                    paths.append(Path(module.project) / settings[key])

        lb_args = Namespace(
            arch_roots=roots['arch_root'],
            board_roots=roots['board_root'],
            soc_roots=roots['soc_root'],
            board=None,
            board_dir=[],
        )
        try:
            boards = list_boards.find_v2_boards(lb_args)
        except RuntimeError as e:
            self.die(str(e))
        systems = list_hardware.find_v2_systems(Namespace(soc_roots=roots['soc_root']))
        return boards, {soc.name: soc for soc in systems.get_socs()}, roots['soc_root']

    def targets(self, spec, boards, socs):
        replacement = undeprecate(spec, self.deprecated_boards())
        if replacement != spec:
            self.note(f'{spec} is deprecated, using {replacement}')
        try:
            targets = resolve_target(replacement, boards, socs)
        except ValueError as e:
            self.die(str(e))
        if len(targets) > 1:
            self.note(
                f'{spec} names {len(targets)} board targets: ' + ', '.join(t.name for t in targets)
            )
        for target in targets:
            self.dbg(
                f'{target.name}: SoC {target.soc.name if target.soc else None}, '
                f'CPU cluster {target.cpucluster}, variants {target.variants}'
            )
        return targets

    def deprecated_boards(self):
        path = ZEPHYR_BASE / 'boards' / 'deprecated.cmake'
        try:
            text = path.read_text(encoding='utf-8')
        except OSError:
            return {}
        return dict(re.findall(r'set\(\s*(\S+)_DEPRECATED\s+(\S+)\s*\)', text))

    def projects(self):
        '''Returns the projects of the manifest, but the manifest project and Zephyr,
        then those of zephyr/west.yml that the manifest does not have.'''
        projects = []
        for project in self.manifest.projects[1:]:
            if Path(project.abspath).resolve() == ZEPHYR_BASE.resolve():
                continue
            cloned = Path(project.abspath).is_dir() and project.is_cloned()
            meta = None
            if cloned:
                meta = zephyr_module.process_module(project.abspath, require_yaml_validation=False)
            projects.append(
                Project(
                    name=project.name,
                    path=project.path,
                    url=project.url,
                    revision=project.revision,
                    cloned=cloned,
                    active=self.manifest.is_active(project),
                    module=meta['name'] if meta else None,
                    depends=list(meta.get('build', {}).get('depends', [])) if meta else [],
                )
            )
        names = {project.name for project in self.manifest.projects}
        projects += [
            Project(p.name, p.path, p.url, p.revision, in_manifest=False)
            for p in manifest_file_projects(ZEPHYR_BASE / 'west.yml')
            if p.name not in names
        ]
        return projects

    def resolve(self, needs, projects):
        '''Maps the modules in needs, and those they depend on, to projects.

        The modules they depend on are added to needs.'''
        found = {}
        todo = list(needs)
        while todo:
            resolved, missing = resolve_modules(todo, projects, self.peek)
            if missing:
                self.die(
                    'no west project provides the modules: '
                    + '; '.join(f'{m} ({", ".join(needs[m])})' for m in missing)
                    + '\n  Hint: a module that is not cloned yet is looked for in '
                    'the project of the same name, or whose path ends with its name.'
                )
            found.update(resolved)
            todo = []
            for module, project in resolved.items():
                for dependency in project.depends:
                    if dependency not in needs:
                        needs[dependency].append(f'needed by module {module}')
                        todo.append(dependency)
        return found

    def peek(self, project):
        if project.name not in self.peeked:
            self.note(f'reading zephyr/module.yml of {project.name} from {project.url}')
            self.peeked[project.name] = peek_module_name(project)
        return self.peeked[project.name]

    def print_projects(self, found, needs, names_only=False, allowlist=False):
        modules = defaultdict(list)
        for module, project in found.items():
            modules[project.name].append(module)
        if not modules:
            self.note(
                'the board targets need no west project; mind that "west update" '
                'without project names updates them all'
            )
            return
        if names_only or allowlist:
            for name in sorted(modules):
                self.inf(f'- {name}' if allowlist else name)
            return
        rows = []
        for name in sorted(modules):
            reasons = list(dict.fromkeys(r for m in modules[name] for r in needs[m]))
            rows.append((name, ', '.join(modules[name]), ', '.join(reasons)))
        widths = [max(len(row[i]) for row in rows) for i in (0, 1)]
        for name, provided, reasons in rows:
            self.inf(f'{name:<{widths[0]}}  {provided:<{widths[1]}}  {reasons}')

    def with_zephyr(self, names):
        '''Returns names, after the project of Zephyr if the manifest imports it: west
        ignores the imports of the projects that manifest.project-filter deactivates.'''
        zephyr = ZEPHYR_BASE.resolve()
        projects = self.manifest.projects[1:]
        return [p.name for p in projects if Path(p.abspath).resolve() == zephyr] + names

    def add_to_filter(self, names):
        current = self.config.get('manifest.project-filter')
        value = project_filter_with(current, self.with_zephyr(names))
        if value != current:
            self.config.set('manifest.project-filter', value)
        self.note(f'manifest.project-filter is "{value}"')

    def update(self, names, needs, args):
        '''Runs "west update" on the projects, then on those of the modules they depend on.'''
        if not names:
            self.note('nothing to update')
        updated = set()
        while names:
            self.west_update(names)
            updated.update(names)
            # Only the projects just cloned tell which modules they provide and depend on
            found = self.resolve(needs, self.projects())
            new = [p for p in found.values() if p.name not in updated]
            self.check_projects(new, args)
            names = sorted({p.name for p in new})
            if names and args.filter:
                self.add_to_filter(names)

    def west_update(self, names):
        '''Runs "west update" on the projects only.

        West refuses "west update <projects>" for projects that come from an import,
        as when the manifest imports zephyr. So this runs a plain "west update", with
        a local configuration in which manifest.project-filter activates only the
        projects, and Zephyr if the manifest imports it, which west updates too.
        '''
        config = configparser.ConfigParser(interpolation=None)
        for option, value in self.config.items(configfile=ConfigFile.LOCAL):
            section, _, key = option.partition('.')
            config.read_dict({section: {key: value}})
        value = project_filter_with(None, self.with_zephyr(names))
        config.read_dict({'manifest': {'project-filter': value}})
        self.dbg(f'running "west update" with manifest.project-filter "{value}"')
        with tempfile.TemporaryDirectory(prefix='west-', ignore_cleanup_errors=True) as tmp:
            local = Path(tmp) / 'config'
            with open(local, 'w', encoding='utf-8') as f:
                config.write(f)
            env = dict(os.environ, WEST_CONFIG_LOCAL=os.fspath(local))
            result = subprocess.run(
                [sys.executable, '-m', 'west', 'update'], cwd=self.topdir, env=env
            )
        if result.returncode != 0:
            self.die(f'"west update" of {" ".join(names)} failed', exit_code=result.returncode)

    def note_secure_firmware(self, target, needs):
        firmware = [module for module in SECURE_FIRMWARE.values() if module in needs]
        if firmware:
            what = f'{firmware[0]} is listed for its secure firmware, but not what that needs'
        else:
            what = 'the projects of its secure firmware are not listed'
        self.note(f'{target.name} is a non-secure target: {what}')

    def note(self, message):
        if self.verbosity >= Verbosity.INF:
            print(f'note: {message}', file=sys.stderr)
