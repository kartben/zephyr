# Task 02: Deduplicate `west sign` tool-path resolution

Please clean up duplicated tool-path lookup logic in `/tmp/workspace/kartben/zephyr/scripts/west_commands/sign.py`.

## Goal

Unify the repeated `--tool-path` / PATH lookup code used by the signing backends so `rimage`, `silabs_commander`, and any other supported signers use one consistent mechanism.

## Scope

- `/tmp/workspace/kartben/zephyr/scripts/west_commands/sign.py`
- `/tmp/workspace/kartben/zephyr/scripts/west_commands/tests/`
- `/tmp/workspace/kartben/zephyr/doc/develop/west/sign.rst` if behavior or wording needs updating

## Requirements

1. Replace the ad hoc logic around the `# TODO: use get_tool_path` and `# TODO: replace with get_tool_path` comments with a shared implementation.
2. Preserve the existing supported inputs:
   - explicit `--tool-path`
   - PATH lookup
   - `rimage.path` config handling where applicable
   - `--if-tool-available` behavior for the rimage flow
3. Keep user-facing failures actionable. If messages change, they should still tell the user exactly what is missing and how to fix it.
4. Avoid breaking existing signing flows that do not use the cleaned-up helper.
5. Add focused regression tests under the existing `scripts/west_commands/tests` area.
6. Update documentation only if command behavior, precedence, or error wording meaningfully changes.

## Out of scope

- Do not redesign the whole `west sign` command.
- Do not remove existing supported configuration knobs.
- Do not add a new dependency.

## Validation

At minimum:

- Run the focused pytest coverage for the new or updated west-command tests.
- Run any existing import/style checks that are already used for touched Python files.

## Deliverable

A surgical cleanup that removes duplicated lookup code, keeps behavior compatible, and adds tests covering the shared path-resolution logic.
