<!--
SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
SPDX-License-Identifier: Apache-2.0
-->

# Reproducing the SPDX 3.1 demo SBOM

Branch-local notes for regenerating the "Zephyr RTOS (SPDX 3.1)" sample used by the
[SPDX 3 visualizer](https://kartben.github.io/spdx3_viz/#s=zephyr-experimental).

This branch is `main` plus two stacks of `west spdx` work:

**Hardware profile** (4 commits cherry-picked from `spdx_3.1_hw_profile`):

| Commit | What it adds |
| --- | --- |
| `emit target board in the SPDX 3.1 Hardware profile` | A `hardware_PhysicalHardware` element for `BOARD`, a `runsOn` edge from the final image, and `hardware` profile conformance on the document that carries it. |
| `add devicetree hardware extraction for SPDX 3.1` | `zspdx/devicetree.py`: every enabled, binding-backed node in `edt.pickle` becomes a `hardware_PhysicalHardware` component, with `contains` edges mirroring the devicetree hierarchy and the vendor resolved from `dts/bindings/vendor-prefixes.txt`. |
| `enhance devicetree hardware extraction with binding type` | Adds a `devicetree-binding-type:<type>` external identifier (the binding's top-level directory under `dts/bindings`), omitted for `misc`. |
| `use a Zephyr-controlled namespace for custom identifiers` | Custom identifiers get a Zephyr issuing authority; `build_buildType` moves from `urn:spdx.dev:zephyr-cmake` to `urn:zephyrproject.org:build-type:cmake`. |

**Requirement traceability** (10 commits rebased from `claude/rebase-spdx-hw-reqs-rvu67y`): the
`@satisfies` / `@verifies` Doxygen commands, the StrictDoc catalog reader, the twister importer,
and the `Requirement` / FunctionalSafety elements built from them. See
[`doc/develop/west/zephyr-cmds.rst`](doc/develop/west/zephyr-cmds.rst) for the user-facing
description of `--requirements-dir` and `--twister-out`.

**Driver linkage** (1 commit on top): `scripts: zspdx: link drivers to the devicetree nodes
they instantiate` parses the linker map for the `__device_dts_ord_<N>` symbols
`DEVICE_DT_DEFINE()` emits and links each devicetree node to the driver source that
instantiates it.

Both are no-ops for `--spdx-version 2.3` / `3.0`: the Hardware profile and the `Requirement`
class did not exist before SPDX 3.1.

## Prerequisites

* A Zephyr workspace with this branch checked out as the `zephyr` project.
* Zephyr SDK with the `xtensa-espressif_esp32s3` toolchain (the sample targets an ESP32-S3
  board) and a host toolchain for QEMU x86.
* The workspace Python venv: `west`, plus `spdx-tools` and `spdx-python-model` (the
  serializer picks the `v3_1` bindings for `--spdx-version 3.1`).
* The `reqmgmt` module cloned — `west update reqmgmt` puts it in `<workspace>/doc/reqmgmt`.

## Run it

```bash
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR="$HOME/zephyr-sdk-1.0.1"

BUILD=/tmp/spdx31-build
TWOUT=/tmp/spdx31-twister
OUT=/tmp/spdx-3.1
REQDIR="$(west topdir)/doc/reqmgmt"

# 1. Build. CONFIG_BUILD_OUTPUT_META is what makes the build emit zephyr.meta and
#    request the CMake file-based API; `west spdx --init` is no longer needed.
west build -p always -b m5stack_paper_color/esp32s3/procpu \
  -d "$BUILD" samples/modules/lvgl/demos \
  -- -DCONFIG_BUILD_OUTPUT_META=y

# 2. Run the annotated tests, so @verifies links get a pass/fail verdict to carry.
./scripts/twister -p qemu_x86 -T tests/kernel/threads --outdir "$TWOUT" -j 8

# 3. Generate every document, with header analysis, the SDK BOM and both
#    requirement sources.
west spdx -d "$BUILD" --spdx-version 3.1 --analyze-includes --include-sdk \
  --requirements-dir "$REQDIR" --twister-out "$TWOUT" -s "$OUT"
```

What each flag contributes:

* `--analyze-includes` re-runs the compiler with `-E -H` on every translation unit so the
  headers each source pulls in become files in their own right. **`@satisfies` traceability
  depends on it**, because those annotations live in headers that are never compiled directly.
* `--include-sdk` adds the `sdk` document that the toolchain headers among those belong to.
* `--requirements-dir` points at the `reqmgmt` StrictDoc catalog, which supplies the
  human-readable statement for each requirement UID. Auto-detected from
  `ZEPHYR_REQMGMT_MODULE_DIR` or the west manifest when omitted — but *not* when `ZEPHYR_BASE`
  is a git worktree, since the candidate paths are relative to it.
* `--twister-out` reads `twister.json` for the `@verifies` half.

The first two flags are also what makes this slow (a few minutes, dominated by the per-file
compiler pass and the license scan). Steps 1 and 2 are independent and can run in parallel.

Any board works; `m5stack_paper_color/esp32s3/procpu` is what the hosted sample uses. Pick a
board with a `zephyr,display` chosen node, since the LVGL sample requires one (the demo itself
defaults to `CONFIG_LV_Z_DEMO_MUSIC`).

`tests/kernel/threads` is the suite the `@verifies` annotations on this branch cover. It must
run on a platform that supports userspace — `native_sim` statically filters all 21
configurations, `qemu_x86` runs 17 of them.

### Running from a git worktree

`west` resolves extension commands through the manifest, so `west spdx` runs the copy of
`scripts/west_commands/spdx.py` in the workspace's `zephyr` project — *not* the worktree you
are standing in, even with `ZEPHYR_BASE` set. To exercise a worktree without touching the
workspace config, point west at a throwaway local config:

```bash
cat > /tmp/west-local-config <<EOF
[manifest]
path = zephyr/.claude/worktrees/<worktree-name>
file = west.yml
project-filter =

[zephyr]
base = zephyr/.claude/worktrees/<worktree-name>
EOF
export WEST_CONFIG_LOCAL=/tmp/west-local-config
```

Both paths are relative to `west topdir`. Without this, `west spdx` rejects
`--requirements-dir` / `--twister-out` as unknown arguments, which is the tell.

## What you get

`$OUT` holds six JSON-LD documents, all `specVersion` `3.1.0` against the
`https://spdx.org/rdf/3.1/` context. Reference run (2026-08-27), 4.2 MB and 8033 elements
total:

| Document | Size | Elements | Imports | Profiles |
| --- | ---: | ---: | ---: | --- |
| `app.jsonld` | 5 KB | 14 | 0 | core, software, simpleLicensing |
| `build.jsonld` | 922 KB | 612 | 1959 | core, software, simpleLicensing, **build**, **hardware** |
| `modules-deps.jsonld` | 143 KB | 273 | 0 | core, software, simpleLicensing |
| `requirements.jsonld` | 47 KB | 83 | 0 | core, software, simpleLicensing |
| `sdk.jsonld` | 74 KB | 158 | 0 | core, software, simpleLicensing |
| `zephyr.jsonld` | 3.1 MB | 6893 | 78 | core, software, simpleLicensing |

They cross-reference each other through `ExternalMap`/`locationHint` (`build` alone imports
1959 ids from the other documents), so load them **together** — drop all six onto the
visualizer at once, or use its file picker.

### `build.jsonld` — the Hardware profile

* 41 `hardware_PhysicalHardware` elements — the board plus one per enabled devicetree node
  (40 for this board);
* 40 `contains` edges reproducing the devicetree hierarchy under the board;
* `runsOn` from `zephyr.elf` and `zephyr_pre0.elf` to the board;
* external identifiers on the devicetree components: 40 `devicetree-compatible`, 40
  `devicetree-path`, 39 `devicetree-binding-type` (omitted for `misc`);
* 33 of the 41 components resolve a vendor, drawn from six `Organization` elements referenced
  as `hardware_productAgent` — E Ink, Espressif, M5Stack, Seiko Epson, Sensirion, Worldsemi;
* 28 `build_Build` elements, `build_buildType` `urn:zephyrproject.org:build-type:cmake`.

### `zephyr.jsonld` — driver-to-hardware linkage

11 `configures` relationships, each from a driver source to the `hardware_PhysicalHardware`
for the devicetree node it instantiates — `i2c_esp32.c` to `/soc/i2c@60013000`,
`display_ed2208_gca.c` to `/mipi_dbi/ed2208_doa@0`, and so on. Both `espressif,esp32-spi`
instances resolve to `spi_esp32_spim.c` separately, because the link is keyed on the node's
dependency ordinal rather than on its compatible.

They live in `zephyr.jsonld` (which owns the driver sources) and point into `build.jsonld`
(which owns the hardware), so they arrive as ExternalMap imports — this is what takes
`zephyr.jsonld` from 67 imports to 78.

Of the 15 devices the build instantiates, 11 link: the remaining four are nodes
`_is_hardware()` does not emit a component for — `/mipi_dbi` (a `zephyr,*` abstraction) and
the three `pmic@6e/regulators/*` child-binding nodes, which have no `matching_compat`.

### `zephyr.jsonld` — `@satisfies` traceability

13 `Requirement` elements with 13 `implementedBy` relationships pointing at the annotated
headers — 7 to `irq.h` and 6 to `kernel.h`. This is the half that needs `--analyze-includes`.
The annotations in `device.h`, `init.h` and `sleep.h` produce nothing in this run because
every UID they reference is missing from the catalog (see below).

### `requirements.jsonld` — `@verifies` traceability

The document that did not exist in the July sample. From the twister run:

* 7 `software_Package` elements, one per test suite that carries `@verifies` annotations,
  each tagged with a `twister-run:<run_id>` external identifier;
* 12 `functionalsafety_RequirementVerification` elements (`verificationMethod` = `test`),
  each carrying the `@details` prose of the annotated test as its description;
* 12 `functionalsafety_EvaluationResult` elements — all `pass` for this run — with a
  rationale naming the suite and platform;
* 12 `functionalsafety_EvidenceRelationship` elements;
* 8 `Requirement` elements, linked by 14 `verifiedBy`, 12 `hasTest` and 12 `hasEvidence`
  relationships.

## Known gaps

Ordered roughly by how likely they are to matter.

* **`requirements.jsonld` cannot declare FunctionalSafety conformance.** SPDX 3.1 ships the
  `functionalsafety_*` classes but its `ProfileIdentifierType` enumeration has no
  `functionalSafety` member (`core`, `software`, `simpleLicensing`, `expandedLicensing`,
  `build`, `ai`, `dataset`, `security`, `hardware`, `supplyChain`, `extension`, `lite`). So
  the document carries 36 FunctionalSafety elements while conforming, on paper, only to
  core/software/simpleLicensing. Nothing to fix on our side — a consumer that filters by
  `profileConformance` will simply not see this document as safety-related.
* **15 referenced requirement UIDs have no catalog entry.** The run loads 288 requirements
  from `reqmgmt` at the pinned `c6803e9b4e9`, but the annotations reference UIDs that a
  matching `reqmgmt` change has not landed yet — `ZEP-SRS-1-14/15/16/17/19/20`, `12-1`,
  `14-3/4/5/6/9/10`, `28-8/9`. Those links are dropped with a warning rather than emitting a
  dangling `Requirement`, which is why `zephyr.jsonld` shows 13 requirements and not 28.
* **Vendor ids contain spaces.** `normalize_spdx_name()` only maps `_` to `-`, so five of the
  six vendors yield ids such as `zephyr:vendors/Espressif Systems`, which is not a legal IRI.
* **The board element carries no external identifiers.** `_create_hardware_object()` reads
  `ARCH` and `CMAKE_SYSTEM_PROCESSOR` from `CMakeCache.txt`, but Zephyr sets neither as a
  cache entry, so `target-arch` / `target-processor` are silently dropped. The 40 devicetree
  components are unaffected — their identifiers come from the EDT, not the cache.
* **Fewer CMake targets than the July sample** (29 packages vs 74). Not a regression:
  `1872775fb44 ("scripts: zspdx: exclude CMake UTILITY targets from the SBOM")` landed in
  `main` since, so phony targets like `flash`, `menuconfig` and `debug` no longer appear.
* The `west spdx` progress output says "Written SPDX 3.0 JSON-LD" even for 3.1. Cosmetic.
