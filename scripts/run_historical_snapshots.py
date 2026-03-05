#!/usr/bin/env python3

# Copyright (c) 2025
# SPDX-License-Identifier: Apache-2.0

"""Collect boards-per-SOC-vendor snapshots every 3 months using git worktrees.

Creates temporary worktrees for historical commits, runs the snapshot script,
and saves results to a JSON file for graphing evolution over time.

Note: Some older commits may fail due to board/soc schema changes. Failed
dates are skipped; run from zephyr repo root.
"""

import argparse
import json
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

# Add scripts dir for imports
SCRIPTS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS_DIR))

# Zephyr repo root (parent of scripts/)
ZEPHYR_BASE = SCRIPTS_DIR.parent


def run_git(cwd: Path, *args: str) -> str:
    result = subprocess.run(
        ['git'] + list(args),
        cwd=cwd,
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout.strip()


def get_commit_at_date(repo: Path, date: str) -> str | None:
    """Get the latest commit on or before the given date (YYYY-MM-DD)."""
    try:
        return run_git(repo, 'log', '-1', '--format=%H', f'--before={date}')
    except subprocess.CalledProcessError:
        return None


def collect_snapshots(
    zephyr_base: Path,
    output_file: Path,
    start_year: int = 2020,
    interval_months: int = 3,
) -> None:
    """Collect snapshots every N months from start_year until now."""
    from boards_per_soc_vendor_snapshot import run_snapshot

    worktree_base = zephyr_base.parent / '.zephyr_snapshot_worktrees'
    worktree_base.mkdir(exist_ok=True)

    snapshots = []
    year = start_year
    month = 1

    while True:
        date_str = f'{year:04d}-{month:02d}-01'
        dt = datetime(year, month, 1)
        if dt > datetime.now():
            break

        commit = get_commit_at_date(zephyr_base, date_str)
        if not commit:
            print(f'Skipping {date_str}: no commit found', file=sys.stderr)
            month += interval_months
            if month > 12:
                month -= 12
                year += 1
            continue

        worktree_path = worktree_base / f'snap_{date_str}'
        try:
            if worktree_path.exists():
                run_git(zephyr_base, 'worktree', 'remove', '--force', str(worktree_path))

            run_git(zephyr_base, 'worktree', 'add', str(worktree_path), commit)
            data = run_snapshot(worktree_path)
            snapshots.append(
                {
                    'date': date_str,
                    'commit': commit[:12],
                    'total_boards': data['total_boards'],
                    'vendors': data['vendors'],
                }
            )
            print(f'{date_str}: {data["total_boards"]} boards, {len(data["vendors"])} vendors')
        except Exception as e:
            print(f'Error at {date_str}: {e}', file=sys.stderr)
        finally:
            if worktree_path.exists():
                try:
                    run_git(zephyr_base, 'worktree', 'remove', '--force', str(worktree_path))
                except subprocess.CalledProcessError:
                    shutil.rmtree(worktree_path, ignore_errors=True)

        month += interval_months
        if month > 12:
            month -= 12
            year += 1

    if worktree_base.exists() and not any(worktree_base.iterdir()):
        worktree_base.rmdir()

    output = {
        'generated': datetime.now().isoformat(),
        'zephyr_base': str(zephyr_base),
        'snapshots': snapshots,
    }
    output_file.write_text(json.dumps(output, indent=2))
    print(f'\nWrote {len(snapshots)} snapshots to {output_file}')


def main():
    parser = argparse.ArgumentParser(
        description='Collect historical boards-per-SOC-vendor snapshots via git worktrees'
    )
    parser.add_argument(
        '--zephyr-base', type=Path, default=ZEPHYR_BASE, help='Zephyr repository path'
    )
    parser.add_argument(
        '-o',
        '--output',
        type=Path,
        default=SCRIPTS_DIR / 'boards_per_soc_vendor_history.json',
        help='Output JSON file',
    )
    parser.add_argument('--start-year', type=int, default=2023, help='First year to snapshot')
    parser.add_argument('--interval-months', type=int, default=3, help='Months between snapshots')
    parser.add_argument(
        '--current-only',
        action='store_true',
        help='Snapshot current tree only (no worktrees, for testing)',
    )
    args = parser.parse_args()

    if args.current_only:
        from boards_per_soc_vendor_snapshot import run_snapshot
        from datetime import datetime

        data = run_snapshot(args.zephyr_base.resolve())
        commit = run_git(args.zephyr_base, 'rev-parse', '--short', 'HEAD')
        output = {
            'generated': datetime.now().isoformat(),
            'zephyr_base': str(args.zephyr_base),
            'snapshots': [
                {
                    'date': datetime.now().strftime('%Y-%m-%d'),
                    'commit': commit,
                    'total_boards': data['total_boards'],
                    'vendors': data['vendors'],
                }
            ],
        }
        args.output.write_text(json.dumps(output, indent=2))
        print(f'Wrote current snapshot to {args.output}')
        return

    if not (args.zephyr_base / '.git').is_dir():
        sys.exit(f'ERROR: {args.zephyr_base} is not a git repository')

    collect_snapshots(
        args.zephyr_base.resolve(),
        args.output,
        args.start_year,
        args.interval_months,
    )


if __name__ == '__main__':
    main()
