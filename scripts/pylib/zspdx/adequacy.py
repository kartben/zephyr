# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Compute the "true traceability" adequacy of a requirement.

A requirement is only genuinely verified when the tests that verify it actually
execute the code that implements it. This adjudicates that from the resolved
implementation bodies (:mod:`zspdx.sources`) and the coverage matrix
(:mod:`zspdx.coverage`), mirroring ``doc/_scripts/traceability_app.py``.

A syscall-capable API has several bodies (``z_impl_``/``z_vrfy_``/inline); a
symbol counts as exercised when *any* of its bodies is. A body that no test in
the whole run covers cannot be adjudicated by coverage (boot-time code, inlined
away, config'd out) and must not count against the requirement.
"""

from __future__ import annotations

# Verdicts, worst-first for reporting.
BROKEN = "broken"
PARTIAL = "partial"
TRUE = "true"
UNATTRIBUTED = "unattributed"
NO_COV = "no-cov"
UNRESOLVED = "unresolved"
NO_IMPL = "no-impl"

VERDICT_HELP = {
    TRUE: "every resolved implementing symbol is exercised by the requirement's "
          "own verifying tests",
    PARTIAL: "some implementing symbols are exercised by the verifying tests, others not",
    BROKEN: "the verifying tests never execute the implementing code — other tests do",
    UNATTRIBUTED: "no test in the run covered the implementation lines (boot-time, "
                  "inlined away, or config'd out)",
    NO_COV: "the verifying tests have no coverage data in this run",
    UNRESOLVED: "implementation links exist but map to macros/inlines, not bodies",
    NO_IMPL: "the requirement has no implemented_by link",
}


def _body_covered(body, covered) -> bool:
    """Whether any line of ``body`` is in the ``covered`` set for its file."""
    lines = covered.get(body.file)
    if not lines:
        return False
    return any(ln in lines for ln in range(body.start, body.end + 1))


def requirement_adequacy(symbols, tests, impl_bodies, cov_by_test, all_covered) -> str:
    """Return the adequacy verdict for one requirement.

    Args:
        symbols: the requirement's ``implemented_by`` symbol names.
        tests: the requirement's verifying test ids.
        impl_bodies: ``{symbol: [ImplBody, ...]}`` from
            :func:`zspdx.sources.resolve_impl_symbols`.
        cov_by_test: ``{test_id: {file: set(lines)}}`` (verifying-test coverage).
        all_covered: ``{file: set(lines)}`` covered by any test in the run.
    """
    if not symbols:
        return NO_IMPL

    tests_with_cov = [t for t in tests if t in cov_by_test]
    resolved = adjudicable = own_hits = 0
    for symbol in symbols:
        bodies = impl_bodies.get(symbol)
        if not bodies:
            continue
        resolved += 1
        if any(_body_covered(body, all_covered) for body in bodies):
            adjudicable += 1
        if any(
            _body_covered(body, cov_by_test[test])
            for test in tests_with_cov
            for body in bodies
        ):
            own_hits += 1

    if not resolved:
        return UNRESOLVED
    if not tests_with_cov:
        return NO_COV
    if not adjudicable:
        return UNATTRIBUTED
    if own_hits == adjudicable:
        return TRUE
    if own_hits:
        return PARTIAL
    return BROKEN
