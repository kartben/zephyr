# Copyright (c) 2019 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import os
import re
import sys
import textwrap
from pathlib import Path

from west.commands import CommandError, WestCommand

from zephyr_ext_common import ZEPHYR_BASE

sys.path.append(os.fspath(Path(__file__).parent.parent))
import board_description
import list_boards
import zephyr_module


class Boards(WestCommand):

    def __init__(self):
        super().__init__(
            'boards',
            '',
            description='Display information about boards',
            accepts_unknown_args=False)

    def do_add_parser(self, parser_adder):
        default_fmt = '{name}'
        parser = parser_adder.add_parser(
            self.name,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=self.description,
            epilog=textwrap.dedent(f'''\
            FORMAT STRINGS
            --------------

            Boards are listed using a Python 3 format string. Arguments
            to the format string are accessed by name.

            The default format string is:

            "{default_fmt}"

            The following arguments are available:

            - name: board name
            - full_name: board full name (typically, its commercial name)
            - revision_default: board default revision
            - revisions: list of board revisions
            - qualifiers: board qualifiers (will be empty for legacy boards)
            - dir: directory that contains the board definition
            - vendor: board vendor

            DESCRIBE
            --------

            With --describe, the board's base devicetree is generated for
            each selected board target and described as JSON instead. The
            Zephyr CMake package produces the devicetree, stopping as soon
            as it is available: no Kconfig is run, no toolchain is needed
            and nothing is compiled. Targets are selected with --target, or
            default to every target of every listed board.
            '''))

        # Remember to update west-completion.bash if you add or remove
        # flags
        parser.add_argument('-f', '--format', default=default_fmt,
                            help='''Format string to use to list each board;
                                    see FORMAT STRINGS below.''')
        parser.add_argument('-n', '--name', dest='name_re',
                            help='''a regular expression; only boards whose
                            names match NAME_RE will be listed''')
        parser.add_argument('-a', '--all-targets', action='store_true',
                            help='''Output all valid combinations of {name},
                            {revisions}, and {qualifiers} that can be used as a board
                            target''')
        parser.add_argument('--describe', action='store_true',
                            help='''Describe the devicetree of board targets as
                            JSON, without configuring a build; see DESCRIBE
                            below''')
        board_description.add_args(parser)
        list_boards.add_args(parser)

        return parser

    def do_run(self, args, _):
        if args.name_re is not None:
            name_re = re.compile(args.name_re)
        else:
            name_re = None

        module_settings = {
            'arch_root': [ZEPHYR_BASE],
            'board_root': [ZEPHYR_BASE],
            'soc_root': [ZEPHYR_BASE],
        }

        # CMake discovers the module roots on its own, so only the roots
        # given on the command line have to be passed on to it.
        extra_roots = {
            'ARCH_ROOT': list(args.arch_roots),
            'BOARD_ROOT': list(args.board_roots),
            'SOC_ROOT': list(args.soc_roots),
        }

        for module in zephyr_module.parse_modules(ZEPHYR_BASE, self.manifest):
            for key in module_settings:
                root = module.meta.get('build', {}).get('settings', {}).get(key)
                if root is not None:
                    module_settings[key].append(Path(module.project) / root)

        args.arch_roots += module_settings['arch_root']
        args.board_roots += module_settings['board_root']
        args.soc_roots += module_settings['soc_root']

        all_boards = list_boards.find_v2_boards(args)
        if name_re is None:
            boards = all_boards
        else:
            boards = {n: b for n, b in all_boards.items() if name_re.search(n)}

        if args.describe:
            self.describe_boards(args, all_boards, boards, extra_roots)
            return

        all_targets: list[str] = []
        for board in boards.values():
            if args.all_targets:
                all_targets += [f"{board.name}/{qualifier}"
                                for qualifier in list_boards.board_v2_qualifiers(board)]
                if board.revisions:
                    all_targets += [f"{board.name}@{revision.name}/{qualifier}"
                                    for qualifier in list_boards.board_v2_qualifiers(board)
                                    for revision in board.revisions]
            else:
                if board.revisions:
                    revisions_list = ','.join([rev.name for rev in board.revisions])
                else:
                    revisions_list = 'None'

                self.inf(
                    args.format.format(
                        name=board.name,
                        full_name=board.full_name,
                        revision_default=board.revision_default,
                        revisions=revisions_list,
                        dir=board.dir,
                        hwm=board.hwm,
                        vendor=board.vendor,
                        qualifiers=list_boards.board_v2_qualifiers_csv(board),
                    )
                )

        if args.all_targets:
            self.inf(os.linesep.join(all_targets))

    def describe_boards(self, args, all_boards, selected_boards, extra_roots):
        # A --target may name a board the --name filter does not select, or
        # even an alias of one, so the describer is given every board and the
        # filter only decides which targets are described by default.
        try:
            describer = board_description.BoardDescriber(
                all_boards,
                roots=extra_roots,
                workspace_dir=Path(self.topdir) if self.topdir else None,
                preprocessor=args.preprocessor,
            )
        except board_description.BoardDescriptionError as e:
            self.die(str(e))

        targets = args.targets or describer.all_targets(selected_boards)
        failures = board_description.describe_all(
            describer, targets, args.output_dir, jobs=args.jobs, out=sys.stdout, err=self.err
        )
        if failures:
            raise CommandError(1)
