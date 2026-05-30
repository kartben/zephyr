# Task 13: Review for array bounds and indexing mistakes

Please review the target code area for array indexing mistakes and out-of-bounds risks.

## Goal

Find code that may read or write before the first element or after the last element of an
array, table, FIFO, or buffer.

## Scope

- The target files or subsystem you are asked to review
- Related macros, table definitions, and fixed-size buffer declarations

## Requirements

1. Focus on indexing patterns such as `i + 1`, `i - 1`, `idx++`, and `ARRAY_SIZE(...)`.
2. Check code that accesses adjacent elements inside loops.
3. Look for bounds checks that happen after an element is already accessed.
4. Treat fixed lookup tables and hardware descriptor arrays as high-priority targets.
5. Report only concrete suspects with file paths, line numbers, and a short rationale.

## Out of scope

- Dynamic memory lifetime analysis
- Pure style observations
- Suggestions that are not tied to a specific access pattern

## Deliverable

A shortlist of likely indexing defects, each showing the suspicious access pattern and why it
could exceed valid bounds.
