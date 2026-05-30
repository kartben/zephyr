# Task 03: Fix duplicate maintainer counting in `set_assignees.py`

Please fix the duplicate-counting bug called out in `scripts/ci/set_assignees.py`.

## Goal

Ensure the maintainer/reviewer ranking logic does not over-count a changed file multiple times when that file belongs to multiple matching areas owned by the same maintainer.

## Scope

- `scripts/ci/set_assignees.py`
- Add focused regression tests in an appropriate existing scripts test location
- Use `MAINTAINERS.yml`-style fixtures if needed

## Requirements

1. Address the exact issue noted by the FIXME near the maintainer counting loop.
2. Preserve the current area-label collection behavior unless it is directly affected by the bug.
3. Preserve the distinction between platform-instance areas and higher-level subsystem/meta areas.
4. Make the fix deterministic and easy to reason about. A reviewer should be able to see why a maintainer is counted once versus multiple times.
5. Add regression coverage that demonstrates:
   - one file mapped to multiple areas with the same maintainer
   - one file mapped to multiple areas with different maintainers
   - no unintended regression in assignee/reviewer selection order
6. Keep the patch focused on counting/ranking correctness; do not do a broad rewrite of the script.

## Out of scope

- Do not redesign the full PR triage policy.
- Do not change unrelated reviewer limits or label limits unless required by the bug.

## Validation

At minimum:

- Run the targeted pytest coverage for the new regression tests.
- If you introduce helper functions, keep them covered by the same tests.

## Deliverable

A bugfix with regression tests proving that overlapping MAINTAINERS areas no longer inflate ownership counts for the same maintainer.
