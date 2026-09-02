# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

'''west mcp: serve the workspace to AI coding agents over the Model
Context Protocol.'''

import argparse
import textwrap

from west.commands import WestCommand

from zephyr_mcp.config import ServerConfig

MISSING_MCP = '''\
"west mcp" needs the optional "mcp" Python package (>= 2.1), which is not
installed. Install it with:

    pip install "mcp>=2.1"

or install all optional packages with "west packages pip --install".'''


class Mcp(WestCommand):
    def __init__(self):
        super().__init__(
            'mcp',
            '',
            description='''Run a Model Context Protocol (MCP) server over
            standard input/output that lets an AI coding agent query and
            drive this west workspace.''',
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=self.description,
            epilog=textwrap.dedent('''\
            The server is meant to be launched by an MCP client (an AI
            coding assistant or IDE), not interactively. It exposes tools to
            inspect the workspace, boards, build directories and twister
            results, and to run "west build" and twister. Flashing hardware
            is only offered with --allow-hardware.

            Do not run it with "west -v": debug output printed before the
            server starts would corrupt the protocol stream.'''),
        )

        # Remember to update west-completion.bash if you add or remove
        # flags
        parser.add_argument(
            '--allow-hardware',
            action='store_true',
            help='''offer the flash tool and allow twister options
                            that drive hardware''',
        )
        parser.add_argument(
            '--root',
            metavar='DIR',
            action='append',
            help='''directory the agent may build in or read from;
                            may be given more than once (default: the workspace)''',
        )
        parser.add_argument(
            '--log-dir',
            metavar='DIR',
            help='''where to keep the full logs of builds and test runs
                            (default: a temporary directory)''',
        )
        parser.add_argument(
            '--check',
            action='store_true',
            help='''do not serve; print what the server would report about
                            the workspace and its environment, then exit''',
        )

        return parser

    def do_run(self, args, _):
        cfg = ServerConfig.from_command(self, args)
        if args.check:
            self.check(cfg)
            return

        try:
            from zephyr_mcp import server
        except ImportError as e:
            self.dbg(f'import failed: {e}')
            self.die(MISSING_MCP)

        server.run_stdio(cfg)

    def check(self, cfg):
        from zephyr_mcp.workspace import workspace_info

        info = workspace_info(cfg)
        env = info['environment']
        self.inf(f'workspace: {info["topdir"]}')
        self.inf(f'zephyr: {info["zephyr_version"]} at {info["zephyr_base"]}')
        self.inf(f'projects: {info["projects_cloned"]}/{info["projects_total"]} cloned')
        self.inf(f'python: {env["python"]}')
        for name, path in env['tools'].items():
            self.inf(f'{name}: {path or "NOT FOUND"}')
        self.inf(f'sdk: {", ".join(env["sdk_registry"]) or env["sdk_install_dir"] or "none"}')
        self.inf(f'toolchain variant: {env["toolchain_variant"] or "(default)"}')
        self.inf(f'allowed roots: {", ".join(str(r) for r in cfg.roots)}')
        self.inf(f'hardware: {"allowed" if cfg.allow_hardware else "not allowed"}')
        try:
            import mcp  # noqa: F401
        except ImportError:
            self.wrn('the mcp package is not installed; "west mcp" cannot serve')
        for hint in env['hints']:
            self.wrn(hint)
