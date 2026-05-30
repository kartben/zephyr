# Task 06: Replace the placeholder DSA documentation section with accurate content

Please clean up `/tmp/workspace/kartben/zephyr/doc/services/connectivity/networking/dsa.rst` and remove the current placeholder section.

## Goal

Replace the `TODO work` section with accurate, code-backed documentation of what Zephyr’s current DSA support actually does today, especially around packet flow and supported operating modes.

## Scope

- `/tmp/workspace/kartben/zephyr/doc/services/connectivity/networking/dsa.rst`
- Reference implementation files as needed, including:
  - `/tmp/workspace/kartben/zephyr/subsys/net/ip/net_core.c`
  - `/tmp/workspace/kartben/zephyr/subsys/net/l2/ethernet/dsa/`
  - `/tmp/workspace/kartben/zephyr/dts/bindings/dsa/` if needed

## Requirements

1. Remove the placeholder heading and replace it with real documentation.
2. Base the updated text on what the current Zephyr code supports, not on aspirational Linux parity.
3. Explicitly distinguish:
   - what is implemented now
   - what is not implemented yet
   - what assumptions users must understand about conduit/CPU/user ports
4. Keep the tone and structure consistent with the rest of the networking docs.
5. Avoid speculative statements unless they are clearly labeled as future work.
6. If you discover inaccuracies elsewhere in the page while doing this work, fix them only if they are directly related to the same topic.

## Out of scope

- Do not implement new DSA features.
- Do not rewrite the entire networking documentation set.

## Validation

At minimum:

- Run an existing documentation build target that includes this page.
- Check for Sphinx warnings introduced by the rewritten section.

## Deliverable

A documentation-only patch that removes the placeholder content and replaces it with precise, up-to-date guidance tied to the current implementation.
