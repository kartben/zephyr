# Task 20: Review for broken symmetry across similar implementations

Please review a group of similar implementations for places where one file or instance breaks
the common pattern in a suspicious way.

## Goal

Find likely bugs by comparing multiple implementations that should be structurally similar and
flagging differences that look accidental rather than intentional.

## Scope

- The target family of files or subsystem implementations you are asked to compare
- The shared headers, macros, and helper code needed to understand expected symmetry

## Requirements

1. Compare similar implementations and identify outliers in logic, indexing, register use, or bounds handling.
2. Treat single-file deviations in otherwise uniform code as high-priority suspects.
3. Verify whether the divergence is justified by hardware-specific definitions before flagging it.
4. Prefer findings that point to a likely real defect instead of a mere style difference.
5. Report only concrete suspects with file paths, line numbers, and a short rationale.

## Out of scope

- Broad architectural cleanup proposals
- Pure formatting inconsistencies
- Differences that are clearly explained by documented hardware variation

## Deliverable

A shortlist of high-signal asymmetry findings, each with the compared files or code paths and
a brief explanation of why the deviation appears buggy.
