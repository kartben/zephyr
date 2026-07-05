# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Index a twister per-test line-coverage matrix (``test_matrix.json``).

The matrix records, for every test instance, which source lines it executed.
Its keys are ``<scenario slug>_<test fn>`` and do not carry the ztest suite name,
so the suite-qualified test id (matching the traceability graph and
:mod:`zspdx.twister`) is recovered via the ``qual_map`` built from the twister
results. This mirrors ``doc/_scripts/traceability_app.py``.

Only tree files (``kernel/``, ``include/``, ``lib/``, ``tests/``) are kept, and
— to bound memory against the multi-hundred-MB matrix — only the coverage of the
tests actually referenced by the traceability graph.
"""

from __future__ import annotations

import json
import logging
import re

_logger = logging.getLogger(__name__)


def slugify(name: str) -> str:
    return re.sub(r"[^a-zA-Z0-9]", "_", name)


def _keep_file(path: str) -> bool:
    return path.startswith(("kernel/", "include/", "lib/", "tests/"))


def load_coverage(
    matrix_path: str, qual_map: dict, wanted_tests: set[str] | None = None
) -> tuple[dict, dict]:
    """Index a coverage matrix into per-test coverage and a whole-run union.

    Returns ``(cov_by_test, all_covered)`` where

    * ``cov_by_test[qualified_test_id][file]`` -- set of lines that test covered
      (restricted to ``wanted_tests`` when given), used to prove a requirement's
      own verifying tests exercise its implementation, and
    * ``all_covered[file]`` -- set of lines covered by *any* test in the run (from
      ``by_line``), used to tell an implementation that no test reaches at all
      (``unattributed``) from one only *other* tests reach (``broken``).

    Args:
        matrix_path: Path to ``test_matrix.json``.
        qual_map: ``{scenario: {bare test_ name: qualified id}}`` from
            :func:`zspdx.twister.load_results`.
        wanted_tests: If given, ``cov_by_test`` is restricted to these ids.
    """
    try:
        with open(matrix_path, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        _logger.warning("coverage: could not read %s: %s", matrix_path, e)
        return {}, {}

    # Longest scenario slug wins when several are prefixes of a matrix key.
    slugs = sorted(
        ((slugify(scenario), fns) for scenario, fns in qual_map.items()),
        key=lambda kv: -len(kv[0]),
    )
    # A bare "test_" name resolves directly when it is unique across scenarios.
    unique: dict[str, str | None] = {}
    for fns in qual_map.values():
        for bare, qualified in fns.items():
            unique[bare] = qualified if unique.get(bare, qualified) == qualified else None

    def split_key(key: str) -> str | None:
        for slug, fns in slugs:
            if key.startswith(slug + "_") and key[len(slug) + 1:].startswith("test_"):
                bare = key[len(slug) + 1:]
                return fns.get(bare) or unique.get(bare) or bare
        m = re.search(r"(test_[A-Za-z0-9_]+)$", key)
        return (unique.get(m.group(1)) or m.group(1)) if m else None

    cov_by_test: dict[str, dict[str, set]] = {}
    for key, files in data.get("by_test", {}).items():
        test_id = split_key(key)
        if not test_id or (wanted_tests is not None and test_id not in wanted_tests):
            continue
        dst = cov_by_test.setdefault(test_id, {})
        for path, lines in files.items():
            if _keep_file(path):
                dst.setdefault(path, set()).update(int(x) for x in lines)

    all_covered: dict[str, set] = {}
    for path, lines in data.get("by_line", {}).items():
        if _keep_file(path):
            all_covered[path] = {int(ln) for ln in lines}

    _logger.info(
        "coverage: indexed %d test(s) and %d file(s) of run coverage from %s",
        len(cov_by_test), len(all_covered), matrix_path,
    )
    return cov_by_test, all_covered
