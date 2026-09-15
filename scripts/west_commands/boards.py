# Copyright (c) 2019 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import os
import re
import sys
import textwrap
from pathlib import Path

from west.commands import WestCommand

from zephyr_ext_common import add_json_arg, emit_json, module_roots

sys.path.append(os.fspath(Path(__file__).parent.parent))
import list_boards

DEFAULT_FMT = '{name}'


def board_targets(board):
    '''Return every "name[@revision]/qualifiers" target string for board.'''
    qualifiers = list_boards.board_v2_qualifiers(board)
    targets = [f'{board.name}/{qualifier}' for qualifier in qualifiers]
    targets += [f'{board.name}@{revision.name}/{qualifier}'
                for qualifier in qualifiers
                for revision in board.revisions]
    return targets


def board_to_dict(board):
    '''Return the JSON-serializable representation of a list_boards.Board.'''
    directories = board.directories
    if not isinstance(directories, list):
        directories = [directories]
    return {
        'name': board.name,
        'full_name': board.full_name,
        'vendor': board.vendor,
        'hwm': board.hwm,
        'dir': board.dir,
        'dirs': directories,
        'revision_format': board.revision_format,
        'revision_default': board.revision_default,
        'revision_exact': board.revision_exact,
        'revisions': board.revisions,
        'socs': board.socs,
        'variants': board.variants,
        'qualifiers': list_boards.board_v2_qualifiers(board),
        'targets': board_targets(board),
    }


class Boards(WestCommand):

    def __init__(self):
        super().__init__(
            'boards',
            '',
            description='Display information about boards',
            accepts_unknown_args=False)

    def do_add_parser(self, parser_adder):
        default_fmt = DEFAULT_FMT
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
            '''))

        # Remember to update west-completion.bash if you add or remove
        # flags
        parser.add_argument('-f', '--format',
                            help='''Format string to use to list each board;
                                    see FORMAT STRINGS below.''')
        add_json_arg(parser, help='''print boards (or, with --all-targets,
                                     board targets) as JSON''')
        parser.add_argument('-n', '--name', dest='name_re',
                            help='''a regular expression; only boards whose
                            names match NAME_RE will be listed''')
        parser.add_argument('-a', '--all-targets', action='store_true',
                            help='''Output all valid combinations of {name},
                            {revisions}, and {qualifiers} that can be used as a board
                            target''')
        list_boards.add_args(parser)

        return parser

    def do_run(self, args, _):
        if args.json and args.format is not None:
            self.die('--json and --format are mutually exclusive')
        fmt = args.format or DEFAULT_FMT

        if args.name_re is not None:
            name_re = re.compile(args.name_re)
        else:
            name_re = None

        roots = module_roots(self.manifest, ['arch_root', 'board_root', 'soc_root'])
        args.arch_roots += roots['arch_root']
        args.board_roots += roots['board_root']
        args.soc_roots += roots['soc_root']

        try:
            boards = list_boards.find_v2_boards(args).values()
        except RuntimeError as e:
            self.die(e)
        boards = [b for b in boards if name_re is None or name_re.search(b.name)]

        if args.json:
            if args.all_targets:
                emit_json(sorted(t for b in boards for t in board_targets(b)))
            else:
                emit_json([board_to_dict(b) for b in sorted(boards, key=lambda b: b.name)])
            return

        all_targets: list[str] = []
        for board in boards:
            if args.all_targets:
                all_targets += board_targets(board)
            else:
                if board.revisions:
                    revisions_list = ','.join([rev.name for rev in board.revisions])
                else:
                    revisions_list = 'None'

                self.inf(
                    fmt.format(
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
