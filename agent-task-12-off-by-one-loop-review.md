# Task 12: Review for off-by-one loop errors

Please review the target code area for likely off-by-one mistakes in loops and bounds checks.

## Goal

Find loops, counters, and boundary checks that may run one step too far or stop one step too
early.

## Scope

- The target files or subsystem you are asked to review
- Any small supporting definitions needed to understand the intended bounds

## Requirements

1. Prioritize loops using `<=`, reverse loops, sentinel termination, and hand-written index arithmetic.
2. Check whether first and last valid elements are handled correctly.
3. Look for mismatches between array length definitions and loop termination conditions.
4. Highlight cases where an inclusive bound would access one element past the end.
5. Report only concrete suspects with file paths, line numbers, and a short rationale.

## Out of scope

- Pure formatting or naming issues
- General performance tuning
- Theoretical issues with no direct evidence in code

## Deliverable

A shortlist of likely off-by-one defects, including the exact loop or condition that appears
incorrect and why.
