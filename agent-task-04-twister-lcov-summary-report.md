# Task 04: Add an LCOV summary artifact in Twister coverage output

Please implement the LCOV summary-report TODO in `scripts/pylib/twister/twisterlib/coverage.py`.

## Goal

When Twister runs coverage with the LCOV backend, generate a lightweight summary artifact
in addition to the existing `.info` and HTML outputs, and return its path in the coverage
result metadata.

## Scope

- `scripts/pylib/twister/twisterlib/coverage.py`
- `scripts/tests/twister_blackbox/test_coverage.py`
- `doc/develop/test/coverage.rst` if user-visible output changes need documenting

## Requirements

1. Implement the TODO at the LCOV HTML-generation stage without changing gcovr behavior.
2. Produce a summary artifact that is easy to inspect in CI logs or downloaded artifacts.
3. Populate the returned `summary` field instead of leaving it as `None` for the LCOV path when the summary is generated.
4. Keep current output formats working:
   - LCOV-only output
   - LCOV + HTML output
   - existing gcovr formats
5. Add focused blackbox or unit-style regression coverage for the new artifact and metadata.
6. Do not break existing file naming conventions unless there is a strong reason.

## Out of scope

- Do not redesign Twister’s overall coverage pipeline.
- Do not make unrelated changes to gcovr handling.

## Validation

At minimum:

- Run the focused Twister coverage tests that exercise LCOV output.
- Verify that existing expected files are still produced.

## Deliverable

A focused feature/cleanup patch that adds a real LCOV summary output, wires it into the
returned metadata, and includes regression coverage.
