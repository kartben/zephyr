# Task 09: Sanitize backport titles for both RST and JSON output

Please implement the title-sanitization TODO in `scripts/release/list_backports.py`.

## Goal

Make `Backport.sanitize_title()` produce titles that are safe and readable in both
reStructuredText output and JSON output.

## Scope

- `scripts/release/list_backports.py`
- Add focused regression tests in an appropriate existing scripts test location

## Requirements

1. Replace the current no-op implementation of `sanitize_title()`.
2. Handle at least the common characters and patterns that can break or degrade either:
   - RST list output
   - inline `:github:` references
   - JSON serialization / readability
3. Keep titles human-readable; do not over-sanitize into something unnatural.
4. Ensure both `print()` and `print_json()` continue to use the same sanitization policy.
5. Add regression tests for representative titles, including punctuation, backticks, quotes, colons, and titles that already contain issue/PR references.
6. Keep the change local to title handling; do not rework GitHub API access or issue discovery logic.

## Out of scope

- Do not change output schema for `print_json()`.
- Do not rewrite the script’s CLI.

## Validation

At minimum:

- Run the focused regression tests you add for the release script.
- Manually inspect a few sanitized examples in both plain-text and JSON form.

## Deliverable

A small but solid bugfix/cleanup patch that makes backport output robust for both RST and
JSON consumers.
