# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

'''The MCP server itself: the only module that depends on the mcp SDK.'''

import asyncio
import json
import sys
from contextlib import asynccontextmanager
from typing import Any
from urllib.parse import unquote

from mcp.server.mcpserver import Context, MCPServer
from mcp.types import ToolAnnotations

from zephyr_ext_common import ZephyrJSONEncoder
from zephyr_mcp import build as build_mod
from zephyr_mcp import builddir, proc, twister, workspace

READ_ONLY = ToolAnnotations(
    read_only_hint=True, destructive_hint=False, idempotent_hint=True, open_world_hint=False
)
BUILDS = ToolAnnotations(
    read_only_hint=False, destructive_hint=False, idempotent_hint=True, open_world_hint=False
)
HARDWARE = ToolAnnotations(
    read_only_hint=False, destructive_hint=True, idempotent_hint=False, open_world_hint=True
)

INSTRUCTIONS = '''\
You are connected to a Zephyr RTOS west workspace at {topdir} (ZEPHYR_BASE: {zephyr_base}).
This server runs on the host next to the workspace; it is unrelated to the MCP server library
that Zephyr applications can run on a device (CONFIG_MCP_SERVER).

Workspace model: the manifest lists projects (git repositories) that "west update" checks out;
Zephyr modules are the projects that carry a zephyr/module.yml. "west update" is deliberately
not exposed here, run it in a shell if the workspace is stale.

Boards: a build target is "board[@revision]/soc[/cpucluster][/variant]". Use list_boards to
search and board_info to get the exact "targets" a board accepts; do not guess qualifiers.
native_sim and the qemu_* targets run on the host without hardware.

Building: build(source_dir, board, ...) wraps "west build". build_dir defaults to "build" in the
workspace. pristine="auto" re-runs CMake when the board or application changes; use
pristine="always" after editing Kconfig fragments or overlays outside the application, or when
a build directory is in doubt. Pass snippets, shields, extra_conf, extra_dtc_overlay and
cmake_args through the corresponding parameters rather than crafting a shell command. Sysbuild
builds have several domains; build_dir_info reports them and most build-directory tools accept
a domain parameter.

Reading results: the build result carries parsed diagnostics and the tail of the log, with
log_path pointing at the full log. Use kconfig and devicetree_query on a build directory
instead of grepping generated headers, and runners_info to see how it can be flashed.

Testing: twister_run wraps twister. Narrow the run with paths (test directories), scenarios,
platforms and tags; build_only=true or platforms=["native_sim"] give quick answers. Inspect an
existing run with twister_results(outdir). list_tests reports the discoverable test cases.

Hardware: the flash tool exists only when the server was started with --allow-hardware
(currently: {hardware}). Always pass runner and domain explicitly when runners_info lists more
than one.

Long operations (build, twister_run, flash) report progress, can be cancelled, and run one at a
time. Every path must lie under: {roots}.
'''


def _j(obj):
    # Normalize paths, sets and dataclasses to plain JSON types.
    return json.loads(json.dumps(obj, cls=ZephyrJSONEncoder))


def build_server(cfg) -> MCPServer:
    @asynccontextmanager
    async def lifespan(server):
        # The stdio transport already points fd 1 at stderr; make sure any
        # print() from west or its helpers goes there too.
        sys.stdout = sys.stderr
        try:
            yield {}
        finally:
            proc.kill_all_live()

    mcp = MCPServer(
        'zephyr',
        instructions=INSTRUCTIONS.format(
            topdir=cfg.topdir,
            zephyr_base=cfg.zephyr_base,
            hardware='allowed' if cfg.allow_hardware else 'not allowed',
            roots=', '.join(str(r) for r in cfg.roots),
        ),
        lifespan=lifespan,
    )
    heavy = asyncio.Lock()

    @mcp.tool(annotations=READ_ONLY)
    def workspace_info() -> dict[str, Any]:
        '''Describe the west workspace: paths, versions, build defaults and what this
        server allows.'''
        return _j(workspace.workspace_info(cfg))

    @mcp.tool(annotations=READ_ONLY)
    def list_projects(cloned_only: bool = False, with_sha: bool = False) -> dict[str, Any]:
        '''List the manifest projects with their path, URL, revision and clone state.'''
        return _j(workspace.list_projects(cfg, cloned_only, with_sha))

    @mcp.tool(annotations=READ_ONLY)
    def list_modules() -> dict[str, Any]:
        '''List the Zephyr modules (projects with a zephyr/module.yml) and their build
        integration settings.'''
        return _j(workspace.list_modules(cfg))

    @mcp.tool(annotations=READ_ONLY)
    def list_boards(
        name_re: str | None = None, vendor: str | None = None, limit: int = 200
    ) -> dict[str, Any]:
        '''Search the supported boards by name regex and/or vendor. Returns their
        revisions, SoCs, variants and qualifiers; use board_info for the full target list.'''
        return _j(workspace.list_boards(cfg, name_re, vendor, limit))

    @mcp.tool(annotations=READ_ONLY)
    def board_info(board: str) -> dict[str, Any]:
        '''Describe one board, including every "name[@revision]/qualifiers" target string
        accepted by build(), and where its definition and documentation live.'''
        return _j(workspace.board_info(cfg, board))

    @mcp.tool(annotations=READ_ONLY)
    async def list_tests(
        paths: list[str] | None = None, tags: list[str] | None = None
    ) -> dict[str, Any]:
        '''List the twister test cases discovered under the given test roots (default: the
        whole tree), optionally restricted by tag.'''
        return _j(await twister.list_tests(cfg, paths or [], tags or []))

    @mcp.tool(annotations=READ_ONLY)
    def build_dir_info(build_dir: str | None = None) -> dict[str, Any]:
        '''Describe a build directory: board, sysbuild domains, CMake cache highlights,
        build_info.yml and the artifacts it contains.'''
        return _j(builddir.build_dir_info(cfg, build_dir))

    @mcp.tool(annotations=READ_ONLY)
    def kconfig(
        build_dir: str | None = None,
        symbols: list[str] | None = None,
        pattern: str | None = None,
        domain: str | None = None,
    ) -> dict[str, Any]:
        '''Look up Kconfig values in a build directory's .config, by symbol name(s) or by
        regex. Unset booleans are reported as "n".'''
        return _j(builddir.kconfig(cfg, build_dir, symbols, pattern, domain))

    @mcp.tool(annotations=READ_ONLY)
    def devicetree_query(
        build_dir: str | None = None,
        compatible: str | None = None,
        label: str | None = None,
        chosen: str | None = None,
        path: str | None = None,
        status: str = 'okay',
        domain: str | None = None,
    ) -> dict[str, Any]:
        '''Query the final devicetree of a build by compatible, node label, chosen name or
        node path. status="any" includes disabled nodes.'''
        return _j(
            builddir.devicetree_query(
                cfg, build_dir, compatible, label, chosen, path, status, domain
            )
        )

    @mcp.tool(annotations=READ_ONLY)
    def runners_info(build_dir: str | None = None, domain: str | None = None) -> dict[str, Any]:
        '''Report how a build directory can be flashed and debugged: available runners,
        defaults per command and the common runner configuration.'''
        return _j(builddir.runners_info(cfg, build_dir, domain))

    @mcp.tool(annotations=READ_ONLY)
    def twister_results(
        outdir_or_json: str,
        status: list[str] | None = None,
        name_re: str | None = None,
        limit: int = 100,
        include_log: bool = False,
    ) -> dict[str, Any]:
        '''Read an existing twister.json (or the twister output directory holding it) and
        return a summary plus the matching test instances.'''
        return _j(twister.twister_results(cfg, outdir_or_json, status, name_re, limit, include_log))

    @mcp.tool(annotations=BUILDS)
    async def build(
        ctx: Context,
        source_dir: str,
        board: str | None = None,
        build_dir: str | None = None,
        pristine: str = 'auto',
        sysbuild: bool | None = None,
        snippets: list[str] | None = None,
        shields: list[str] | None = None,
        extra_conf: list[str] | None = None,
        extra_dtc_overlay: list[str] | None = None,
        cmake_args: list[str] | None = None,
        target: str | None = None,
        timeout_s: int = 1800,
    ) -> dict[str, Any]:
        '''Build an application with "west build". Reports ninja progress and returns the
        parsed diagnostics and the tail of the log.'''
        async with heavy:
            return _j(
                await build_mod.build(
                    cfg,
                    ctx.report_progress,
                    source_dir,
                    board,
                    build_dir,
                    pristine,
                    sysbuild,
                    snippets or [],
                    shields or [],
                    extra_conf or [],
                    extra_dtc_overlay or [],
                    cmake_args or [],
                    target,
                    timeout_s,
                )
            )

    @mcp.tool(annotations=BUILDS)
    async def twister_run(
        ctx: Context,
        paths: list[str] | None = None,
        scenarios: list[str] | None = None,
        platforms: list[str] | None = None,
        tags: list[str] | None = None,
        build_only: bool = False,
        outdir: str | None = None,
        clobber: bool = False,
        jobs: int | None = None,
        extra_args: list[str] | None = None,
        timeout_s: int = 7200,
    ) -> dict[str, Any]:
        '''Run twister. Reports progress, then returns the summary and the failed instances;
        query details with twister_results(outdir). clobber=true deletes an existing outdir
        instead of renaming it.'''
        async with heavy:
            return _j(
                await twister.twister_run(
                    cfg,
                    ctx.report_progress,
                    paths or [],
                    scenarios or [],
                    platforms or [],
                    tags or [],
                    build_only,
                    outdir,
                    clobber,
                    jobs,
                    extra_args or [],
                    timeout_s,
                )
            )

    if cfg.allow_hardware:

        @mcp.tool(annotations=HARDWARE)
        async def flash(
            ctx: Context,
            build_dir: str | None = None,
            runner: str | None = None,
            domain: str | None = None,
            dev_id: str | None = None,
            rebuild: bool = True,
            extra_args: list[str] | None = None,
            timeout_s: int = 600,
        ) -> dict[str, Any]:
            '''Flash a build onto the connected board with "west flash". Programs real
            hardware.'''
            async with heavy:
                return _j(
                    await build_mod.flash(
                        cfg,
                        ctx.report_progress,
                        build_dir,
                        runner,
                        domain,
                        dev_id,
                        rebuild,
                        extra_args or [],
                        timeout_s,
                    )
                )

    def dumps(obj) -> str:
        return json.dumps(obj, cls=ZephyrJSONEncoder, indent=2, sort_keys=True)

    def build_id_to_dir(build_id: str) -> str:
        return str(cfg.topdir / unquote(build_id))

    @mcp.resource('zephyr://workspace', mime_type='application/json')
    def workspace_resource() -> str:
        '''The workspace description, as returned by workspace_info.'''
        return dumps(workspace.workspace_info(cfg))

    @mcp.resource('zephyr://build/{build_id}/info', mime_type='application/json')
    def build_info_resource(build_id: str) -> str:
        '''build_dir_info for the build directory build_id (relative to the workspace,
        "/" percent-encoded).'''
        return dumps(builddir.build_dir_info(cfg, build_id_to_dir(build_id)))

    @mcp.resource('zephyr://build/{build_id}/config', mime_type='text/plain')
    def build_config_resource(build_id: str) -> str:
        '''The zephyr/.config of the build directory build_id.'''
        app = builddir.domain_build_dir(builddir.resolve_build_dir(cfg, build_id_to_dir(build_id)))
        return (app / 'zephyr' / '.config').read_text()

    @mcp.resource('zephyr://build/{build_id}/runners', mime_type='application/json')
    def build_runners_resource(build_id: str) -> str:
        '''runners_info for the build directory build_id.'''
        return dumps(builddir.runners_info(cfg, build_id_to_dir(build_id)))

    return mcp


def run_stdio(cfg):
    build_server(cfg).run(transport='stdio')
