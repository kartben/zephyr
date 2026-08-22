# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

import argparse
import os
import sys
import textwrap
from pathlib import Path

from west.commands import WestCommand

from zephyr_ext_common import ZEPHYR_BASE

sys.path.append(os.fspath(Path(__file__).parent.parent))
import list_modules


class Modules(WestCommand):
    DEFAULT_LIST_FMT = '{name} ({sources})'

    def __init__(self):
        super().__init__(
            'modules',
            '',
            description='List or fetch west projects required by a build',
            accepts_unknown_args=False,
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=self.description,
            epilog=textwrap.dedent(f'''\
            Resolve west projects declared by SoCs, drivers, bindings,
            shields, and applications, then optionally fetch only those
            projects. A board is a query, not the only place deps live.

            Examples:

              west modules list -b nucleo_f401re
              west modules fetch -b nucleo_f401re
              west modules fetch -b nucleo_f401re --shield x_nucleo_iks01a3
              west modules fetch -b qemu_x86 --app samples/modules/lvgl/demos
              west modules fetch --dry-run -b qemu_x86

            Default ``west update`` behavior is unchanged. This command
            is the opt-in path for a minimal workspace. See
            doc/develop/west/modules-opt-in.rst.

            FORMAT STRINGS
            --------------

            The default format string is:

            "{self.DEFAULT_LIST_FMT}"

            Available fields: name, sources, cmake_var
            '''),
        )

        parser.add_argument(
            'subcmd',
            nargs='?',
            default='list',
            choices=['list', 'fetch', 'check'],
            help='sub-command to execute (default: list)',
        )
        parser.add_argument(
            '-b', '--board',
            help='board name or board target to resolve',
        )
        parser.add_argument(
            '--shield',
            dest='shields',
            action='append',
            default=[],
            help='shield name; may be given more than once',
        )
        parser.add_argument(
            '--app',
            dest='app_dir',
            type=Path,
            default=None,
            help='application directory (overlays, prj.conf, tests.yaml)',
        )
        parser.add_argument(
            '--board-root',
            dest='board_roots',
            default=[],
            type=Path,
            action='append',
            help='add a board root, may be given more than once',
        )
        parser.add_argument(
            '--soc-root',
            dest='soc_roots',
            default=[],
            type=Path,
            action='append',
            help='add a SoC root, may be given more than once',
        )
        parser.add_argument(
            '--defaults',
            dest='include_defaults',
            action=argparse.BooleanOptionalAction,
            default=True,
            help='include scripts/modules-defaults.yml (default: true)',
        )
        parser.add_argument(
            '--all-declared',
            action='store_true',
            help='operate on every module declared in YAML or Kconfig',
        )

        group = parser.add_argument_group('west modules list options')
        group.add_argument(
            '-f', '--format',
            help='format string used by list; see FORMAT STRINGS below',
        )

        group = parser.add_argument_group('west modules fetch options')
        group.add_argument(
            '--dry-run',
            action='store_true',
            help='print the projects that would be fetched, then exit',
        )
        group.add_argument(
            '--all',
            action='store_true',
            dest='fetch_all',
            help='fetch every active west project (same as west update)',
        )

        return parser

    def _resolve(self, args):
        board_roots = args.board_roots or [ZEPHYR_BASE]
        soc_roots = args.soc_roots or [ZEPHYR_BASE]

        if args.all_declared:
            return list_modules.list_declared_modules(
                board_roots=board_roots,
                soc_roots=soc_roots,
                zephyr_base=ZEPHYR_BASE,
            )

        if not args.board and not args.shields and not args.app_dir and not args.fetch_all:
            if args.subcmd == 'list':
                return list_modules.list_declared_modules(
                    board_roots=board_roots,
                    soc_roots=soc_roots,
                    zephyr_base=ZEPHYR_BASE,
                )
            self.die(
                'specify -b/--board, --shield, --app, --all-declared, or --all'
            )

        return list_modules.resolve_modules(
            board_name=args.board,
            shields=args.shields,
            board_roots=board_roots,
            soc_roots=soc_roots,
            include_defaults=args.include_defaults,
            zephyr_base=ZEPHYR_BASE,
            app_dir=args.app_dir,
        )

    def _present_project_names(self):
        present = set()
        if self.manifest is None:
            return present
        for project in self.manifest.projects:
            if project.name == 'manifest':
                continue
            if project.is_cloned():
                present.add(project.name)
        return present

    def do_run(self, args, unknown):
        if args.fetch_all:
            if args.subcmd != 'fetch':
                self.die('--all is only valid with fetch')
            if args.dry_run:
                self.inf('would run: west update')
                return
            self._banner('fetching all active west projects')
            self._run_west_update([])
            return

        try:
            resolved = self._resolve(args)
        except RuntimeError as e:
            self.die(str(e))

        if args.subcmd == 'list':
            fmt = args.format or self.DEFAULT_LIST_FMT
            for req in resolved.required:
                self.inf(fmt.format(
                    name=req.name,
                    sources=','.join(req.sources),
                    cmake_var=req.cmake_var,
                ))
            return

        if args.subcmd == 'check':
            present = self._present_project_names()
            missing = resolved.missing(present)
            if not missing:
                self.inf('all required modules are present')
                return
            hint = 'west modules fetch'
            if args.board:
                hint += f' -b {args.board}'
            for shield in args.shields:
                hint += f' --shield {shield}'
            if args.app_dir:
                hint += f' --app {args.app_dir}'
            self.die(
                'missing required modules: '
                + ', '.join(missing)
                + f'\nfetch them with: {hint}'
            )

        # fetch
        names = resolved.names
        if not names:
            self.inf('no required modules declared for this build')
            return

        if args.dry_run:
            for name in names:
                self.inf(name)
            return

        self._banner('fetching required west projects: ' + ', '.join(names))
        self._run_west_update(names)

    def _banner(self, msg):
        self.inf('-- west modules: ' + msg, colorize=True)

    def _run_west_update(self, projects):
        # Enable common optional groups so named HAL (and similar)
        # projects can be fetched even when the workspace filter
        # disabled them. west update still only updates the listed
        # projects when names are given.
        cmd = [
            sys.executable, '-m', 'west', 'update',
            '--group-filter=+hal,+fs,+crypto,+debug,+bootloader,+tee,+tools',
        ]
        cmd.extend(projects)
        self.dbg(' '.join(cmd))
        self.check_call(cmd)
