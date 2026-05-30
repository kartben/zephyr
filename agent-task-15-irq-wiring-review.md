# Task 15: Review for IRQ wiring mistakes

Please review the target code area for interrupt setup mistakes.

## Goal

Find likely bugs in IRQ configuration, ISR wiring, interrupt numbering, priority selection,
and related per-instance setup code.

## Scope

- The target files or subsystem you are asked to review
- Related interrupt macros, device-tree lookups, and per-instance initialization helpers

## Requirements

1. Focus on mismatched IRQ indexes, wrong ISR/device pairings, and suspicious arithmetic on IRQ numbers.
2. Compare repeated per-instance macros and initializer blocks for inconsistencies.
3. Check whether priority values are pulled from the intended source.
4. Distinguish legitimate platform quirks from accidental off-by-one or wrong-index mistakes.
5. Report only concrete suspects with file paths, line numbers, and a short rationale.

## Out of scope

- Interrupt latency tuning
- Style-only observations
- Speculation without a concrete code location

## Deliverable

A shortlist of likely interrupt wiring defects, with the precise setup line and an explanation
of why it looks wrong.
