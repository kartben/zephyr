# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

'''Twister on behalf of the MCP server.'''

import json
import re

from zephyr_mcp.build import ProgressReporter
from zephyr_mcp.paths import resolve_under_roots
from zephyr_mcp.proc import run_west, run_west_capture, west_argv

SELECTED_RE = re.compile(r'(\d+) test scenarios \((\d+) configurations\) selected')
TICKER_RE = re.compile(
    r'Total complete:\s*(\d+)/\s*(\d+)\s+(\d+)%.*?failed:\s*(\d+),\s*error:\s*(\d+)'
)
HARDWARE_ARGS = (
    '--device-testing',
    '--hardware-map',
    '--generate-hardware-map',
    '--device-serial',
    '--device-serial-pty',
    '--device-serial-baud',
    '--flash-before',
    '--west-flash',
    '--west-runner',
)
FAILED_STATUSES = ('failed', 'error')
MAX_FAILED = 50
MAX_LOG_CHARS = 4000


def check_extra_args(extra_args, allow_hardware):
    for arg in extra_args:
        option = arg.split('=', 1)[0]
        if option in HARDWARE_ARGS and not allow_hardware:
            raise ValueError(
                f'{option} drives hardware; start "west mcp --allow-hardware" to permit it'
            )


def twister_argv(
    outdir,
    paths=(),
    scenarios=(),
    platforms=(),
    tags=(),
    build_only=False,
    clobber=False,
    jobs=None,
    extra_args=(),
) -> list[str]:
    argv = ['twister', '-O', str(outdir)]
    if clobber:
        argv.append('-c')
    for path in paths:
        argv += ['-T', str(path)]
    for scenario in scenarios:
        argv += ['-s', scenario]
    for platform in platforms:
        argv += ['-p', platform]
    for tag in tags:
        argv += ['--tag', tag]
    if build_only:
        argv.append('-b')
    if jobs:
        argv += ['-j', str(jobs)]
    argv += list(extra_args)
    return west_argv(*argv)


def list_tests_argv(paths=(), tags=()) -> list[str]:
    argv = ['twister', '--list-tests', '--json']
    for path in paths:
        argv += ['-T', str(path)]
    for tag in tags:
        argv += ['--tag', tag]
    return west_argv(*argv)


async def list_tests(cfg, paths=(), tags=(), timeout_s=300) -> dict:
    paths = [resolve_under_roots(p, cfg.roots, cwd=cfg.topdir) for p in paths]
    res = await run_west_capture(
        cfg, list_tests_argv(paths, tags), log_name='list-tests', timeout_s=timeout_s
    )
    result = {
        'ok': res.returncode == 0 and not res.timed_out,
        'returncode': res.returncode,
        'timed_out': res.timed_out,
        'log_path': res.log_path,
        'tail': res.tail,
    }
    if result['ok']:
        tests = json.loads(res.output)
        result.update(count=len(tests), tests=tests)
    return result


def load_report(path) -> dict:
    with open(path) as f:
        return json.load(f)


def summarize(report) -> dict:
    summary = {}
    for suite in report.get('testsuites', []):
        status = suite.get('status') or 'None'
        summary[status] = summary.get(status, 0) + 1
    summary['total'] = len(report.get('testsuites', []))
    return summary


def _suite_entry(suite, include_log=False) -> dict:
    entry = {
        'name': suite['name'],
        'platform': suite['platform'],
        'toolchain': suite.get('toolchain'),
        'status': suite['status'],
        'reason': suite.get('reason'),
        'execution_time': suite.get('execution_time'),
        'build_time': suite.get('build_time'),
        'testcases': [
            {'identifier': tc['identifier'], 'status': tc['status'], 'reason': tc.get('reason')}
            for tc in suite.get('testcases', [])
        ],
    }
    if include_log:
        entry['log'] = (suite.get('log') or '')[-MAX_LOG_CHARS:]
    return entry


async def twister_run(
    cfg,
    progress,
    paths=(),
    scenarios=(),
    platforms=(),
    tags=(),
    build_only=False,
    outdir=None,
    clobber=False,
    jobs=None,
    extra_args=(),
    timeout_s=7200,
) -> dict:
    check_extra_args(extra_args, cfg.allow_hardware)
    outdir = resolve_under_roots(outdir or 'twister-out', cfg.roots, cwd=cfg.topdir)
    paths = [resolve_under_roots(p, cfg.roots, cwd=cfg.topdir) for p in paths]
    reporter = ProgressReporter(progress)

    async def on_line(line):
        if m := SELECTED_RE.search(line):
            await reporter(0, int(m[2]), line.strip(), force=True)
        elif m := TICKER_RE.search(line):
            done, total, pct, failed, error = (int(g) for g in m.groups())
            await reporter(
                done,
                total,
                f'{done}/{total} ({pct}%) failed={failed} error={error}',
                force=done == total,
            )

    argv = twister_argv(
        outdir, paths, scenarios, platforms, tags, build_only, clobber, jobs, extra_args
    )
    res = await run_west(cfg, argv, log_name='twister', timeout_s=timeout_s, on_line=on_line)

    report_path = outdir / 'twister.json'
    result = {
        'ok': res.returncode == 0 and not res.timed_out,
        'returncode': res.returncode,
        'timed_out': res.timed_out,
        'duration_s': res.duration_s,
        'argv': res.argv,
        'outdir': outdir,
        'twister_json': report_path if report_path.is_file() else None,
        'testplan_json': outdir / 'testplan.json',
        'twister_log': outdir / 'twister.log',
        'log_path': res.log_path,
        'tail': res.tail[-40:],
        'summary': None,
        'failed': [],
    }
    if report_path.is_file():
        report = load_report(report_path)
        result['summary'] = summarize(report)
        failed = [s for s in report['testsuites'] if s.get('status') in FAILED_STATUSES]
        result['failed'] = [_suite_entry(s) for s in failed[:MAX_FAILED]]
        result['failed_truncated'] = len(failed) > MAX_FAILED
    return result


def twister_results(
    cfg, outdir_or_json, status=None, name_re=None, limit=100, include_log=False
) -> dict:
    path = resolve_under_roots(outdir_or_json, cfg.roots, cwd=cfg.topdir)
    if path.is_dir():
        path = path / 'twister.json'
    if not path.is_file():
        raise ValueError(f'{path} not found')
    report = load_report(path)
    pattern = re.compile(name_re) if name_re else None
    suites = [
        s
        for s in report['testsuites']
        if (status is None or s.get('status') in status)
        and (pattern is None or pattern.search(s['name']))
    ]
    return {
        'report': path,
        'environment': {k: v for k, v in report.get('environment', {}).items() if k != 'options'},
        'summary': summarize(report),
        'count': len(suites),
        'truncated': len(suites) > limit,
        'testsuites': [_suite_entry(s, include_log) for s in suites[:limit]],
    }
