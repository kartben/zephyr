# Task 19: Review for size, count, and stride mismatches

Please review the target code area for mistakes involving sizes, counts, units, and strides.

## Goal

Find bugs caused by mixing bytes with elements, counts with lengths, words with bytes, or
loop strides with access patterns.

## Scope

- The target files or subsystem you are asked to review
- Related size macros, struct layouts, and buffer or descriptor definitions

## Requirements

1. Focus on loops that advance by more than one element while still accessing adjacent entries.
2. Check for confusion between byte counts and element counts.
3. Look for code that uses `sizeof(...)` in a way that may not match the intended unit.
4. Treat serialization, DMA, register windows, and protocol buffers as high-priority targets.
5. Report only concrete suspects with file paths, line numbers, and a short rationale.

## Out of scope

- General optimization ideas
- Style-only comments
- Abstract concerns that do not point to a specific mismatch

## Deliverable

A shortlist of likely size/count/stride defects, including the exact unit mismatch or stride
pattern that appears unsafe.
