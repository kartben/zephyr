# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

'''Run west sub-processes on behalf of the MCP server.'''

import asyncio
import contextlib
import os
import re
import subprocess
import sys
import time
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path

import psutil

TAIL_LINES = 200
TAIL_BYTES = 64 * 1024
ANSI_RE = re.compile(r'\x1b\[[0-9;?]*[A-Za-z]')

# Processes started by run_west() that have not exited yet, so that they
# can be killed when the client goes away.
_live: set[asyncio.subprocess.Process] = set()


@dataclass
class ProcResult:
    argv: list[str]
    returncode: int | None
    timed_out: bool
    log_path: str
    tail: list[str] = field(default_factory=list)
    truncated: bool = False
    duration_s: float = 0.0
    output: str | None = None


def west_argv(*args) -> list[str]:
    # Run the west of this interpreter, not whichever one is first on PATH.
    return [sys.executable, '-m', 'west', *map(str, args)]


def kill_tree(pid):
    try:
        parent = psutil.Process(pid)
    except psutil.NoSuchProcess:
        return
    procs = parent.children(recursive=True) + [parent]
    for p in procs:
        with contextlib.suppress(psutil.NoSuchProcess):
            p.terminate()
    _, alive = psutil.wait_procs(procs, timeout=3)
    for p in alive:
        with contextlib.suppress(psutil.NoSuchProcess):
            p.kill()


def kill_all_live():
    for proc in list(_live):
        if proc.returncode is None:
            kill_tree(proc.pid)
    _live.clear()


def _spawn_kwargs():
    if os.name == 'nt':
        return {'creationflags': subprocess.CREATE_NEW_PROCESS_GROUP}
    return {'start_new_session': True}


def _env(cfg):
    return {
        **os.environ,
        'PYTHONUNBUFFERED': '1',
        'NO_COLOR': '1',
        'TERM': 'dumb',
        'ZEPHYR_BASE': str(cfg.zephyr_base),
    }


def log_file(cfg, name) -> Path:
    return Path(cfg.log_dir) / f'{time.strftime("%Y%m%d-%H%M%S")}-{name}.log'


async def run_west(cfg, argv, *, log_name, timeout_s, on_line=None, cwd=None) -> ProcResult:
    '''Run argv, streaming its combined output to a log file.

    on_line, if given, is awaited with every non-empty line (ANSI escapes
    stripped, "\\r" treated as a line break so progress tickers show up).
    The result carries the last TAIL_LINES lines. On timeout the process
    tree is killed; on cancellation it is killed and the cancellation is
    re-raised.'''
    log_path = log_file(cfg, log_name)
    tail: deque[str] = deque(maxlen=TAIL_LINES)
    truncated = False
    start = time.monotonic()

    proc = await asyncio.create_subprocess_exec(
        *argv,
        cwd=str(cwd or cfg.topdir),
        stdin=asyncio.subprocess.DEVNULL,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.STDOUT,
        env=_env(cfg),
        **_spawn_kwargs(),
    )
    _live.add(proc)

    async def pump():
        nonlocal truncated
        pending = b''
        with open(log_path, 'wb') as log:
            while True:
                chunk = await proc.stdout.read(4096)
                if not chunk:
                    break
                log.write(chunk)
                *lines, pending = re.split(rb'\r\n|\r|\n', pending + chunk)
                for raw in lines:
                    line = ANSI_RE.sub('', raw.decode('utf-8', 'replace')).rstrip()
                    if not line:
                        continue
                    if len(tail) == tail.maxlen:
                        truncated = True
                    tail.append(line)
                    if on_line is not None:
                        await on_line(line)
        await proc.wait()

    timed_out = False
    try:
        try:
            await asyncio.wait_for(pump(), timeout_s)
        except TimeoutError:
            timed_out = True
            kill_tree(proc.pid)
            await proc.wait()
    except asyncio.CancelledError:
        kill_tree(proc.pid)
        raise
    finally:
        _live.discard(proc)

    tail_lines = list(tail)
    while sum(len(line) for line in tail_lines) > TAIL_BYTES:
        tail_lines.pop(0)
        truncated = True
    return ProcResult(
        argv=list(argv),
        returncode=proc.returncode,
        timed_out=timed_out,
        log_path=str(log_path),
        tail=tail_lines,
        truncated=truncated,
        duration_s=round(time.monotonic() - start, 2),
    )


async def run_west_capture(cfg, argv, *, log_name, timeout_s, cwd=None) -> ProcResult:
    '''Run argv and capture its complete stdout; stderr goes to the log.'''
    log_path = log_file(cfg, log_name)
    start = time.monotonic()
    proc = await asyncio.create_subprocess_exec(
        *argv,
        cwd=str(cwd or cfg.topdir),
        stdin=asyncio.subprocess.DEVNULL,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
        env=_env(cfg),
        **_spawn_kwargs(),
    )
    _live.add(proc)
    timed_out = False
    stdout = stderr = b''
    try:
        try:
            stdout, stderr = await asyncio.wait_for(proc.communicate(), timeout_s)
        except TimeoutError:
            timed_out = True
            kill_tree(proc.pid)
            await proc.wait()
    except asyncio.CancelledError:
        kill_tree(proc.pid)
        raise
    finally:
        _live.discard(proc)
    Path(log_path).write_bytes(stderr)
    tail = [
        line
        for line in ANSI_RE.sub('', stderr.decode('utf-8', 'replace')).splitlines()
        if line.strip()
    ][-TAIL_LINES:]
    return ProcResult(
        argv=list(argv),
        returncode=proc.returncode,
        timed_out=timed_out,
        log_path=str(log_path),
        tail=tail,
        duration_s=round(time.monotonic() - start, 2),
        output=stdout.decode('utf-8', 'replace'),
    )
