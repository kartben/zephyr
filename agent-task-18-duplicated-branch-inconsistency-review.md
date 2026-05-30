# Task 18: Review for duplicated-branch inconsistencies

Please review the target code area for inconsistent updates across duplicated branches.

## Goal

Find places where parallel `if/else`, `switch`, or per-instance code paths should stay in sync
but one branch appears to be missing an update or using the wrong field.

## Scope

- The target files or subsystem you are asked to review
- Nearby helper code and structs needed to compare the duplicated branches accurately

## Requirements

1. Compare structurally similar branches line by line.
2. Focus on cases where one branch updates a value, flag, or register that the sibling branch does not.
3. Treat near-identical code with a single unexpected difference as suspicious.
4. Distinguish deliberate special-case logic from accidental drift.
5. Report only concrete suspects with file paths, line numbers, and a short rationale.

## Out of scope

- Requests for refactoring duplicated code
- Style differences with no behavior impact
- Findings not tied to a clear branch-to-branch comparison

## Deliverable

A shortlist of likely duplicated-branch bugs, with the compared branches and the specific
inconsistency called out.
