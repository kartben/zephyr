# Copyright (c) 2024 Vestas Wind Systems A/S
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
import list_shields

DEFAULT_FMT = '{name}'


def shield_to_dict(shield):
    '''Return the JSON-serializable representation of a list_shields.Shield.'''
    return {
        'name': shield.name,
        'full_name': shield.full_name,
        'vendor': shield.vendor,
        'dir': shield.dir,
        'supported_features': shield.supported_features,
    }


class Shields(WestCommand):

    def __init__(self):
        super().__init__(
            'shields',
            '',
            description='Display list of supported shields',
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

            Shields are listed using a Python 3 format string. Arguments
            to the format string are accessed by name.

            The default format string is:

            "{default_fmt}"

            The following arguments are available:

            - name: shield name
            - full_name: shield full name (typically, its commercial name)
            - vendor: shield vendor
            - dir: directory that contains the shield definition
            '''))

        # Remember to update west-completion.bash if you add or remove
        # flags
        parser.add_argument('-f', '--format',
                            help='''Format string to use to list each shield;
                                    see FORMAT STRINGS below.''')
        add_json_arg(parser, help='print shields as JSON')
        parser.add_argument('-n', '--name', dest='name_re',
                            help='''a regular expression; only shields whose
                            names match NAME_RE will be listed''')
        list_shields.add_args(parser)

        return parser

    def do_run(self, args, _):
        if args.json and args.format is not None:
            self.die('--json and --format are mutually exclusive')
        fmt = args.format or DEFAULT_FMT

        if args.name_re is not None:
            name_re = re.compile(args.name_re)
        else:
            name_re = None

        args.board_roots += module_roots(self.manifest, ['board_root'])['board_root']

        shields = [s for s in list_shields.find_shields(args)
                   if name_re is None or name_re.search(s.name)]

        if args.json:
            emit_json([shield_to_dict(s) for s in shields])
            return

        for shield in shields:
            self.inf(fmt.format(
                name=shield.name,
                dir=shield.dir,
                vendor=shield.vendor if hasattr(shield, 'vendor') else '',
                full_name=shield.full_name if hasattr(shield, 'full_name') else shield.name
            ))
