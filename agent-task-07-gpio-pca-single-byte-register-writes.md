# Task 07: Reduce unnecessary full-register writes in the PCA GPIO driver

Please address the single-byte write TODO in `drivers/gpio/gpio_pca_series.c`.

## Goal

Stop writing more register data than necessary when configuring a single pin in the PCA-
series GPIO driver, while preserving existing behavior and cache correctness.

## Scope

- `drivers/gpio/gpio_pca_series.c`
- Any directly related helper declarations in the same file
- Existing tests or board-specific validation that can exercise PCA95xx behavior

## Requirements

1. Address the TODO in `gpio_pca_series_pin_configure()` and any directly related logic in interrupt configuration if the same inefficiency exists there.
2. Introduce the smallest reasonable helper/API shape needed to write only the modified byte for 1-byte-per-port registers.
3. Keep register-cache behavior correct when partial writes are used.
4. Do not change behavior for parts/register types that still require wider writes.
5. Preserve endianness and address calculations across the supported PCA variants.
6. Add regression coverage if a realistic focused test can be added; otherwise validate with the best existing targeted coverage and explain the gap in the final notes.

## Out of scope

- Do not broadly rewrite the driver.
- Do not change unrelated GPIO semantics or devicetree bindings.

## Validation

At minimum:

- Run `perl scripts/checkpatch.pl --no-tree -f drivers/gpio/gpio_pca_series.c`
- Run the most focused existing GPIO/PCA validation available in-tree for the touched behavior.

## Deliverable

A small driver cleanup/bugfix patch that narrows writes to the affected byte where
possible without regressing cache synchronization or device support.
