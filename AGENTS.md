# Agent guidance for this repository

This tree is the [Zephyr RTOS](https://www.zephyrproject.org/) source. Use this file as a fast orientation; authoritative detail lives in the [Zephyr documentation](https://docs.zephyrproject.org).

## Scope and layout

- **Kernel and core APIs**: `kernel/`, `include/zephyr/`, `lib/`
- **SoCs and boards**: `soc/`, `boards/`
- **Drivers and subsystems**: `drivers/`, `subsys/`
- **Devicetree**: `dts/`, board and SoC overlays under `boards/` and `soc/`
- **Kconfig**: `Kconfig` files throughout; top-level menus in `Kconfig.zephyr`
- **Samples and tests**: `samples/`, `tests/`
- **Build and CI scripts**: `CMakeLists.txt`, `scripts/` (including `scripts/ci/`)

## Environment assumptions

- Set **`ZEPHYR_BASE`** to the root of this repository (or use **west** from a Zephyr workspace so the environment is configured for you).
- Typical workflow uses **west**, **CMake**, a cross-GNU or vendor toolchain for the target, and **Python 3.12+** for scripts and Twister.
- Full setup: [Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html).

## Making changes

- Follow the project **contribution process** and **DCO**: every commit needs a `Signed-off-by` line. Summary: `CONTRIBUTING.rst`; full guide: [Contributing to Zephyr](https://docs.zephyrproject.org/latest/contribute/index.html).
- Match **coding style** and conventions for the touched area; CI enforces checks (including commit message and license headers where required). When in doubt, mirror nearby code in the same subsystem.
- **Devicetree** changes usually need updates in `dts/`, bindings under `dts/bindings/`, and sometimes board or driver `Kconfig` / CMake glue.
- Prefer **targeted tests** via Twister rather than only building one board locally when behavior is test-covered. Reference: [Twister](https://docs.zephyrproject.org/latest/develop/test/twister.html).

## What not to do

- Do not add large unrelated refactors or new documentation files unless the task requires it.
- Do not strip `Signed-off-by` or otherwise bypass licensing/attribution rules.

## Useful references

| Topic | Location |
|--------|-----------|
| Contribution summary | `CONTRIBUTING.rst` |
| West manifest | `west.yml` |
| Compliance / CI helpers | `scripts/ci/` |

When proposing commands in chat, prefer `west build`, `west flash`, and `west twister` where applicable, consistent with current Zephyr docs.
