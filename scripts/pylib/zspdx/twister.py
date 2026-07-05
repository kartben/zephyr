# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Read test-execution results from a completed twister run.

Indexes ``twister.json`` by the suite-qualified ztest function name
(``<ztest suite>__test_<fn>``) — the same id the traceability graph and the
Doxygen ``@verifies`` links use — so a pass/fail rollup can be attached to each
requirement verification. It also returns the ``qual_map`` needed to join the
per-test coverage matrix (whose keys carry only the scenario slug). This mirrors
``doc/_scripts/traceability_app.py``.
"""

from __future__ import annotations

import json
import logging
import os
from collections import defaultdict

_logger = logging.getLogger(__name__)

# Twister statuses that roll up to a failed / skipped verification.
_FAIL = {"failed", "error"}
_SKIP = {"skipped", "blocked", "not run", "filtered"}


def verdict_from_rollup(rollup: str) -> str:
    """Map a result rollup to a FunctionalSafety verdict."""
    if rollup == "passing":
        return "pass"
    if rollup == "failing":
        return "fail"
    return "inconclusive"


def load_results(twister_json: str) -> tuple[dict, dict, dict]:
    """Return ``(environment, results, qual_map)`` from a twister report.

    * ``results[fn]``   -- ``{"instances": [...], "npass", "nfail", "nskip",
      "rollup"}`` keyed by the qualified ztest name ``<suite>__test_<fn>``.
    * ``qual_map[scenario][bare]`` -- maps a scenario's bare ``test_<fn>`` name to
      that qualified name, used to join the coverage matrix.

    A twister testcase ``identifier`` is ``<scenario>.<ztest suite>.<fn minus
    test_>``; the qualified name is rebuilt from its last two segments (which also
    keeps same-named tests from different suites apart).
    """
    if os.path.isdir(twister_json):
        twister_json = os.path.join(twister_json, "twister.json")
    try:
        with open(twister_json, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        _logger.warning("twister: could not read %s: %s", twister_json, e)
        return {}, {}, {}

    env = data.get("environment", {})
    results: dict[str, dict] = defaultdict(lambda: {"instances": []})
    qual_map: dict[str, dict] = defaultdict(dict)
    for suite in data.get("testsuites", []):
        scenario = suite.get("name", "")
        for case in suite.get("testcases", []):
            parts = case.get("identifier", "").split(".")
            if not parts or not parts[-1]:
                continue
            bare = "test_" + parts[-1]
            fn = parts[-2] + "__" + bare if len(parts) >= 2 else bare
            qual_map[scenario][bare] = fn
            results[fn]["instances"].append(
                {
                    "suite": scenario,
                    "platform": suite.get("platform", ""),
                    "status": (case.get("status") or "").lower(),
                    "time": case.get("execution_time") or "",
                }
            )

    for record in results.values():
        statuses = [i["status"] for i in record["instances"]]
        record["npass"] = sum(s == "passed" for s in statuses)
        record["nfail"] = sum(s in _FAIL for s in statuses)
        record["nskip"] = sum(s in _SKIP for s in statuses)
        record["rollup"] = (
            "failing" if record["nfail"] else
            "passing" if record["npass"] else
            "skipped" if record["nskip"] else "no-run"
        )

    _logger.info("twister: %d qualified test result(s) from %s", len(results), twister_json)
    return env, dict(results), dict(qual_map)
