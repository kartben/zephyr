# Task 08: Add snippet source-file metadata to generated CMake output

Please extend `scripts/snippets.py` so the generated CMake output includes useful source-file provenance.

## Goal

Implement the existing `# TODO: add source file info` so the auto-generated snippets CMake file makes it easy to see which `snippet.yml` files contributed each discovered snippet.

## Scope

- `scripts/snippets.py`
- Add focused regression tests in an appropriate scripts test location if none exist yet
- Update docs only if the generated output contract becomes user-visible enough to warrant it

## Requirements

1. Add source-file information in a way that is helpful for debugging snippet resolution.
2. Preserve the current machine-readable variables unless you intentionally add new ones.
3. Do not break Windows path normalization behavior already present in the script.
4. Keep the generated file readable and deterministic.
5. If you add new CMake variables, make their naming clear and consistent with the existing `SNIPPET_*` pattern.
6. Add regression coverage for:
   - multiple snippets
   - one snippet with multiple source files
   - path normalization where relevant

## Out of scope

- Do not redesign the snippet format.
- Do not change unrelated build-system semantics.

## Validation

At minimum:

- Run the focused regression tests you add for the script.
- Verify the generated output is stable across repeated runs with the same inputs.

## Deliverable

A focused tooling improvement that makes generated snippet metadata easier to audit without breaking existing snippet discovery behavior.
