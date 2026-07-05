# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Ingest requirement-verification evidence from a completed twister run.

An application SBOM is generated from a build that does not compile any tests,
so its ``@verifies`` requirement links are empty. Tests are normally run in a
separate twister pass. This module reads a ``twister.json`` report, and for each
test suite scans its sources for ``@verifies`` requirement tags, so serializers
can record — in the app's SBOM — which requirements were verified by which
suites and whether that verification passed (via the SPDX 3.1 FunctionalSafety
profile).

Granularity is per test *suite*: the ``@verifies`` UIDs found in a suite's
sources are attributed to that suite, with the suite's overall pass/fail status.
Per-test-case attribution (mapping a twister testcase identifier to the specific
``@verifies``-annotated function) is a possible future refinement.
"""

from __future__ import annotations

import json
import logging
import os
from dataclasses import dataclass, field

from .requirements import get_verifies_blocks, should_scan_for_requirements

_logger = logging.getLogger(__name__)

# twister suite statuses that count as a passed / failed verification. Anything
# else (skipped, filtered, not run, error without a run, ...) is inconclusive.
_PASS_STATUSES = frozenset({"passed"})
_FAIL_STATUSES = frozenset({"failed", "blocked", "error"})


@dataclass
class VerificationRecord:
    """A test suite from a twister run and the requirements it verifies.

    ``test_items`` holds one entry per ``@verifies`` doc block found in the
    suite's sources: ``{"uids": [...], "brief": str, "details": str}``. ``uids``
    is the flattened set across all items, kept for convenience.
    """

    uids: list[str]
    suite_name: str
    suite_path: str
    platform: str
    status: str
    run_id: str = ""
    source_files: list[str] = field(default_factory=list)
    test_items: list[dict] = field(default_factory=list)


def status_verdict(status: str) -> str:
    """Normalise a twister suite status to ``pass`` / ``fail`` / ``inconclusive``."""
    normalized = (status or "").lower()
    if normalized in _PASS_STATUSES:
        return "pass"
    if normalized in _FAIL_STATUSES:
        return "fail"
    return "inconclusive"


def _scan_suite_verifies(suite_dir: str) -> tuple[list[str], list[str], list[dict]]:
    """Collect ``@verifies`` blocks and the files carrying them under a suite dir."""
    uids: set[str] = set()
    files: list[str] = []
    items: list[dict] = []
    for root, dirs, filenames in os.walk(suite_dir):
        # Never descend into build output that may sit under a suite directory.
        dirs[:] = [d for d in dirs if d != "build" and not d.startswith("twister-out")]
        for filename in filenames:
            path = os.path.join(root, filename)
            if not should_scan_for_requirements(path):
                continue
            if blocks := get_verifies_blocks(path):
                files.append(path)
                for block in blocks:
                    uids.update(block["uids"])
                    items.append(block)
    return sorted(uids), sorted(files), items


def load_twister_verifications(twister_out_dir: str, zephyr_base: str) -> list[VerificationRecord]:
    """Read ``<twister_out_dir>/twister.json`` into verification records.

    Args:
        twister_out_dir: A completed twister output directory.
        zephyr_base: ZEPHYR_BASE, used to resolve each suite's source directory.

    Returns:
        One :class:`VerificationRecord` per suite that verifies at least one
        requirement; empty if the report is missing/unreadable or nothing is
        verified.
    """
    report = os.path.join(twister_out_dir, "twister.json")
    if not os.path.isfile(report):
        _logger.warning("twister: %s not found", report)
        return []
    try:
        with open(report, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        _logger.warning("twister: could not read %s: %s", report, e)
        return []

    records: list[VerificationRecord] = []
    for suite in data.get("testsuites", []):
        rel_path = suite.get("path")
        if not rel_path:
            continue
        suite_dir = os.path.join(zephyr_base, rel_path) if zephyr_base else rel_path
        if not os.path.isdir(suite_dir):
            _logger.debug("twister: suite source dir %s not found; skipping", suite_dir)
            continue
        uids, files, items = _scan_suite_verifies(suite_dir)
        if not uids:
            continue
        records.append(
            VerificationRecord(
                uids=uids,
                suite_name=suite.get("name", rel_path),
                suite_path=rel_path,
                platform=suite.get("platform", ""),
                status=(suite.get("status") or "").lower(),
                run_id=suite.get("run_id", ""),
                source_files=files,
                test_items=items,
            )
        )

    _logger.info(
        "twister: %d verification record(s) from %s", len(records), twister_out_dir
    )
    return records
