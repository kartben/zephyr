# Task 17: Review for boundary-condition bugs

Please review the target code area for logic that may fail at minimum or maximum boundary
values.

## Goal

Find code that behaves incorrectly for edge cases such as zero length, full buffers, empty
buffers, first element, last element, minimum valid value, or maximum supported value.

## Scope

- The target files or subsystem you are asked to review
- Supporting constants or limits needed to identify the intended valid range

## Requirements

1. Focus on edge-sensitive logic around initialization, cleanup, and state transitions.
2. Check whether zero, one, and maximum values follow the same assumptions as middle values.
3. Look for code that handles normal cases correctly but skips the first or last valid entry.
4. Treat FIFO, DMA, packet, descriptor, and buffer management code as high-priority targets.
5. Report only concrete suspects with file paths, line numbers, and a short rationale.

## Out of scope

- Performance tuning
- Pure style feedback
- Broad test-strategy discussions without a specific code finding

## Deliverable

A shortlist of likely boundary-condition bugs, with the triggering edge case and the exact code
location involved.
