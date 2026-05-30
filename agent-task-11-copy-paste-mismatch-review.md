# Task 11: Review for copy/paste mismatches

Please review the target code area for likely copy/paste mistakes.

## Goal

Find places where code appears to have been duplicated and then only partially updated,
leaving behind a wrong symbol, register, enum, field, device instance, or constant.

## Scope

- The target files or subsystem you are asked to review
- Closely related headers or helper code needed to confirm whether a mismatch is real

## Requirements

1. Focus on duplicated logic where one branch or instance still references the wrong name or value.
2. Compare nearby repeated blocks and identify places where one token differs from the expected pattern.
3. Treat wrong register, bit mask, IRQ, channel, port, or device references as high-priority findings.
4. Distinguish intentional hardware differences from accidental leftovers.
5. Report only concrete suspects with file paths, line numbers, and a short rationale.

## Out of scope

- Pure style issues
- Large refactoring suggestions
- Hypothetical concerns without a concrete code location

## Deliverable

A shortlist of high-signal copy/paste bug suspects, each with a precise location and a brief
explanation of why it looks wrong.
