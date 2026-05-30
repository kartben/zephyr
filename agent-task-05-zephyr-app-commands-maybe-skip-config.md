# Task 05: Fix `:maybe-skip-config:` handling in `zephyr-app-commands`

Please fix the `maybe-skip-config` behavior in `doc/_extensions/zephyr/application.py`.

## Goal

Make the `.. zephyr-app-commands::` directive honor `:maybe-skip-config:` in the west-
generated command path instead of effectively ignoring it.

## Scope

- `doc/_extensions/zephyr/application.py`
- `doc/contribute/documentation/guidelines.rst`
- Any documentation pages whose rendered examples must be updated because of the behavior change

## Requirements

1. Address the existing FIXME in `_generate_west()`.
2. Preserve current output for directive uses that do not set `:maybe-skip-config:`.
3. For directive uses that do set `:maybe-skip-config:`, produce output that clearly reflects the “reuse an existing configured build directory when possible” intent.
4. Keep generated commands valid and copy-pasteable.
5. Avoid broad reformatting of unrelated command examples across the docs tree.
6. Update the directive documentation so the option’s behavior matches the implementation.

## Out of scope

- Do not rewrite the entire directive.
- Do not change unrelated `host-os`, `tool`, or `generator` behavior unless required for correctness.

## Validation

At minimum:

- Run the smallest existing documentation build that covers the directive output, or a standard doc build if no narrower target exists.
- Check that pages using `.. zephyr-app-commands::` still render cleanly.

## Deliverable

A targeted bugfix that makes `:maybe-skip-config:` do what the docs say it does, with any
necessary documentation updates kept minimal and accurate.
