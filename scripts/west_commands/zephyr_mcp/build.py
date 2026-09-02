# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

'''"west build" and "west flash" on behalf of the MCP server.'''

import re
import time

from zephyr_mcp.builddir import resolve_build_dir
from zephyr_mcp.paths import resolve_under_roots
from zephyr_mcp.proc import run_west, west_argv

NINJA_RE = re.compile(r'^\[\s*(\d+)/(\d+)\]')
DIAG_RE = re.compile(
    r'^(?P<file>[^\s:][^:]*?):(?P<line>\d+)(?::(?P<col>\d+))?: '
    r'(?P<severity>fatal error|error|warning): (?P<message>.*)$'
)
STOP_RE = re.compile(r'^(ninja: build stopped|CMake Error|FAILED:|-- west build: .*failed)')
PRISTINE_CHOICES = ('auto', 'always', 'never')
MAX_DIAGNOSTICS = 50


class ProgressReporter:
    '''Forward progress to the client at most once per second.'''

    def __init__(self, report, interval_s=1.0):
        self.report = report
        self.interval_s = interval_s
        self.last = 0.0

    async def __call__(self, progress, total=None, message=None, *, force=False):
        now = time.monotonic()
        if self.report is None or (not force and now - self.last < self.interval_s):
            return
        self.last = now
        await self.report(progress, total, message)


def build_argv(
    source_dir,
    board=None,
    build_dir=None,
    pristine='auto',
    sysbuild=None,
    snippets=(),
    shields=(),
    extra_conf=(),
    extra_dtc_overlay=(),
    cmake_args=(),
    target=None,
) -> list[str]:
    argv = ['build', '-s', str(source_dir), '-p', pristine]
    if board:
        argv += ['-b', board]
    if build_dir:
        argv += ['-d', str(build_dir)]
    if sysbuild is True:
        argv.append('--sysbuild')
    elif sysbuild is False:
        argv.append('--no-sysbuild')
    for snippet in snippets:
        argv += ['-S', snippet]
    for shield in shields:
        argv += ['--shield', shield]
    for conf in extra_conf:
        argv += ['--extra-conf', str(conf)]
    for overlay in extra_dtc_overlay:
        argv += ['--extra-dtc-overlay', str(overlay)]
    if target:
        argv += ['-t', target]
    if cmake_args:
        argv += ['--', *cmake_args]
    return west_argv(*argv)


def parse_diagnostics(lines) -> list[dict]:
    diagnostics = []
    seen = set()
    for line in lines:
        m = DIAG_RE.match(line)
        if m:
            entry = {
                'file': m['file'],
                'line': int(m['line']),
                'col': int(m['col']) if m['col'] else None,
                'severity': m['severity'],
                'message': m['message'],
            }
        elif STOP_RE.match(line):
            entry = {'file': None, 'line': None, 'col': None, 'severity': 'error', 'message': line}
        else:
            continue
        key = tuple(entry.values())
        if key not in seen and len(diagnostics) < MAX_DIAGNOSTICS:
            seen.add(key)
            diagnostics.append(entry)
    return diagnostics


def _result(res, **extra) -> dict:
    return {
        'ok': res.returncode == 0 and not res.timed_out,
        'returncode': res.returncode,
        'timed_out': res.timed_out,
        'duration_s': res.duration_s,
        'argv': res.argv,
        'log_path': res.log_path,
        'tail': res.tail,
        'truncated': res.truncated,
        **extra,
    }


async def build(
    cfg,
    progress,
    source_dir,
    board=None,
    build_dir=None,
    pristine='auto',
    sysbuild=None,
    snippets=(),
    shields=(),
    extra_conf=(),
    extra_dtc_overlay=(),
    cmake_args=(),
    target=None,
    timeout_s=1800,
) -> dict:
    if pristine not in PRISTINE_CHOICES:
        raise ValueError(f'pristine must be one of {", ".join(PRISTINE_CHOICES)}')
    source = resolve_under_roots(source_dir, cfg.roots, cwd=cfg.topdir)
    if not source.is_dir():
        raise ValueError(f'{source} is not a directory')
    build_dir = resolve_under_roots(build_dir or 'build', cfg.roots, cwd=cfg.topdir)
    extra_conf = [resolve_under_roots(f, cfg.roots, cwd=source) for f in extra_conf]
    extra_dtc_overlay = [resolve_under_roots(f, cfg.roots, cwd=source) for f in extra_dtc_overlay]

    reporter = ProgressReporter(progress)

    async def on_line(line):
        m = NINJA_RE.match(line)
        if m:
            await reporter(int(m[1]), int(m[2]), line)

    argv = build_argv(
        source,
        board,
        build_dir,
        pristine,
        sysbuild,
        snippets,
        shields,
        extra_conf,
        extra_dtc_overlay,
        cmake_args,
        target,
    )
    res = await run_west(cfg, argv, log_name='build', timeout_s=timeout_s, on_line=on_line)
    return _result(res, build_dir=build_dir, diagnostics=parse_diagnostics(res.tail))


def flash_argv(
    build_dir, runner=None, domain=None, dev_id=None, rebuild=True, extra_args=()
) -> list[str]:
    argv = ['flash', '-d', str(build_dir)]
    if not rebuild:
        argv.append('--skip-rebuild')
    if domain:
        argv += ['--domain', domain]
    if runner:
        argv += ['-r', runner]
    if dev_id:
        argv += ['--dev-id', dev_id]
    argv += list(extra_args)
    return west_argv(*argv)


async def flash(
    cfg,
    progress,
    build_dir=None,
    runner=None,
    domain=None,
    dev_id=None,
    rebuild=True,
    extra_args=(),
    timeout_s=600,
) -> dict:
    top = resolve_build_dir(cfg, build_dir)
    reporter = ProgressReporter(progress)

    async def on_line(line):
        await reporter(0, None, line)

    argv = flash_argv(top, runner, domain, dev_id, rebuild, extra_args)
    res = await run_west(cfg, argv, log_name='flash', timeout_s=timeout_s, on_line=on_line)
    return _result(res, build_dir=top, runner=runner)
