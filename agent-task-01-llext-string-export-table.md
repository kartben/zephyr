# Task 01: Implement string-link export table sorting for LLEXT

Please implement the currently stubbed string-linking preparation path in `scripts/build/llext_prepare_exptab.py`.

## Goal

Make `_prepare_exptab_for_str_linking()` actually rewrite the LLEXT export table into the sort order required for string-based symbol lookup, instead of logging a warning and doing nothing.

## Scope

- `scripts/build/llext_prepare_exptab.py` - main implementation target for the string-link export-table rewrite
- `include/zephyr/llext/symbol.h` - header describing the export/symbol data layout that must remain compatible
- `subsys/llext/llext.c` - runtime lookup implementation whose sorting assumptions must stay aligned with the preparation script
- `tests/subsys/llext/` - existing subsystem test area for regression coverage

## Requirements

1. Reuse as much of the existing ELF/export-table handling logic as is reasonable, but do not duplicate large blocks of code unnecessarily.
2. Resolve exported symbol names from the relevant ELF/string sections and sort the table by symbol name, not by SLID.
3. Match the runtime lookup assumptions in the LLEXT implementation exactly. If runtime code uses a specific string comparison rule, the preparation step must produce the same ordering.
4. Preserve existing SLID-based behavior. This task is only about the non-`CONFIG_LLEXT_EXPORT_BUILTINS_BY_SLID` path.
5. Keep the export table format unchanged apart from the ordering needed for string lookup.
6. If the preparation step can detect malformed or inconsistent symbol metadata, fail clearly instead of silently generating a broken table.
7. Add or extend regression coverage in the existing LLEXT test area so the string-link path is exercised.

## Out of scope

- Do not redesign the LLEXT ABI.
- Do not change the SLID-based format or lookup behavior unless required for shared helper cleanup.
- Do not broad-refactor unrelated LLEXT code.

## Validation

At minimum:

- Run the smallest focused LLEXT validation you can that exercises built-in symbol lookup.
- If the existing test coverage does not hit the new path, add a targeted regression test and run it.
- Run any style checks needed for touched files.

## Deliverable

A small, reviewable patch that turns the current TODO into working behavior, includes regression coverage, and clearly documents any assumptions you had to preserve between the preparation script and runtime lookup code.
