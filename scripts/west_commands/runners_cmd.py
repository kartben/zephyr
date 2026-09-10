# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

'''west runners: describe the runners west can use and what a build
directory configures for them.'''

import argparse
import textwrap

from west.commands import WestCommand

from build_helpers import FIND_BUILD_DIR_DESCRIPTION
from run_common import (
    INDENT,
    build_runner_context,
    get_build_dir,
    import_module_runners,
    runner_class_info,
)
from runners import ZephyrBinaryRunner
from zephyr_ext_common import add_json_arg, emit_json


class Runners(WestCommand):
    def __init__(self):
        super().__init__(
            'runners',
            '',
            description='''List the runners that "west flash", "west debug" and
            related commands can use, and the runner configuration of a build
            directory.''',
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=self.description,
            epilog=textwrap.dedent('''\
            Without a build directory, only the runner classes known to
            west are described. With one, the runners enabled by the board,
            the default runner for each command and the common runner
            configuration from its runners.yaml are reported as well, per
            domain for sysbuild.

            Unlike "west flash --context", this command never rebuilds the
            application and never prompts.'''),
        )

        # Remember to update west-completion.bash if you add or remove
        # flags
        parser.add_argument(
            '-d',
            '--build-dir',
            metavar='DIR',
            help=f'build directory to describe; {FIND_BUILD_DIR_DESCRIPTION}',
        )
        parser.add_argument('-r', '--runner', help='only describe runner RUNNER')
        parser.add_argument(
            '--domain', action='append', help='only describe the given sysbuild domain(s)'
        )
        add_json_arg(parser, help='print runners and configuration as JSON')

        return parser

    def do_run(self, args, _):
        import_module_runners(self)

        classes = sorted(ZephyrBinaryRunner.get_runners(), key=lambda cls: cls.name())
        if args.runner:
            classes = [cls for cls in classes if cls.name() == args.runner]
            if not classes:
                names = ', '.join(cls.name() for cls in ZephyrBinaryRunner.get_runners())
                self.die(f'unknown runner {args.runner}; choices: {names}')

        result = {
            'build_dir': None,
            'sysbuild': False,
            'domains': [],
            'runners': [runner_class_info(cls) for cls in classes],
        }
        build_dir = get_build_dir(args, die_if_none=False, config=self.config)
        if build_dir:
            result.update(build_runner_context(build_dir, args.domain))

        if args.json:
            emit_json(result)
            return

        self.inf('runners:', colorize=True)
        for runner in result['runners']:
            caps = [k for k, v in runner['capabilities'].items() if v is True]
            self.inf(
                f'{INDENT}{runner["name"]}: {", ".join(runner["commands"])}'
                + (f' ({", ".join(caps)})' if caps else '')
            )

        if not build_dir:
            self.wrn('no --build-dir given or found; output is limited to runner classes')
            return

        self.inf(
            f'build directory: {build_dir}' + (' (sysbuild)' if result['sysbuild'] else ''),
            colorize=True,
        )
        for domain in result['domains']:
            prefix = f'{INDENT}domain {domain["name"]}: ' if domain['name'] else INDENT
            self.inf(f'{prefix}board {domain["board"]}, build_dir {domain["build_dir"]}')
            if domain['runners_yaml'] is None:
                self.inf(f'{INDENT}{INDENT}no runners.yaml; a pristine build may be needed')
                continue
            self.inf(f'{INDENT}{INDENT}available runners: {", ".join(domain["available"])}')
            defaults = ', '.join(f'{k}={v}' for k, v in domain['default'].items())
            self.inf(f'{INDENT}{INDENT}default runners: {defaults}')
            for key, value in domain['config'].items():
                if value not in (None, [], ''):
                    self.inf(f'{INDENT}{INDENT}{key}: {value}')
