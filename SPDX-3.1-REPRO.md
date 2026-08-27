<!--
SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
SPDX-License-Identifier: Apache-2.0
-->

# Reproducing the SPDX 3.1 demo SBOM

Branch-local notes for regenerating the "Zephyr RTOS (SPDX 3.1)" sample used by the
[SPDX 3 visualizer](https://kartben.github.io/spdx3_viz/#s=zephyr-experimental).

This branch stacks three pieces of `west spdx` work on top of current `main`, so one SBOM
carries all of them:

| Commits | What they add |
| --- | --- |
| 4, from `spdx_3.1_hw_profile` | The SPDX 3.1 **Hardware** profile: a `hardware_PhysicalHardware` for the board, one per enabled binding-backed devicetree node, `contains` edges mirroring the devicetree hierarchy, vendors resolved from `vendor-prefixes.txt`, and a Zephyr-controlled namespace for custom identifiers. |
| 1 | **Driver linkage**: the `__device_dts_ord_<N>` symbols `DEVICE_DT_DEFINE()` emits, read out of the linker map, become `configures` edges from each driver source to the node it instantiates. |
| 13, from `collab-safety-spdx-jul5` | The **FunctionalSafety** engine: coverage-backed requirement traceability emitted into a standalone `safety.jsonld`. |

Why the stack matters: the FunctionalSafety work was developed on a branch that forks from
`main` in late June, so on its own it has no Hardware profile and predates the NTIA
minimum-elements work upstream — a sample built from it scores 57/100 for completeness
against this branch's 82/100.

All of it is a no-op for `--spdx-version 2.3` / `3.0`: neither the Hardware profile nor the
`Requirement` class existed before SPDX 3.1.

## The three generated inputs

The safety graph is *coverage-backed*: it does not merely record that a test claims to verify
a requirement, it checks the test actually executed the implementing code. That needs three
inputs, none of which come from the application build:

| Input | Produced by | Feeds |
| --- | --- | --- |
| `traceability.json` | the documentation build (`mlx.traceability` + Doxygen) | `Requirement`, `Specification`, the requirement↔test↔implementation graph |
| `twister.json` | a twister run | per-test pass/fail → `functionalsafety_EvaluationResult` |
| `test_matrix.json` | the same run, with `--coverage-per-test` | per-test covered line ranges → `software_Snippet` + `EvidenceRelationship` |

Miss one and the safety document degrades quietly rather than failing, so check the counts
`west spdx` prints (see [Expected output](#expected-output)).

The requirement *annotations* those inputs are derived from (`@satisfies` / `@verifies` across
`kernel/` and `tests/`) live on `collab-safety-spdx-jul5`, not here — this branch carries the
engine, not the annotation sweep. So steps 1 and 2 below are run from a checkout of that
branch, and only step 3 onwards from this one.

## Prerequisites

* Zephyr SDK with `xtensa-espressif_esp32s3` (the app) and `arm-zephyr-eabi` (the tests).
* The workspace venv: `west`, `spdx-tools`, `spdx-python-model`, plus `strictdoc`,
  `mlx.traceability` and Sphinx for the doc build.
* `lcov` on the host — `--coverage-per-test` refuses to run without it.
* Doxygen (1.18 works).
* `reqmgmt` from the **`collab-requirements`** branch — 529 requirements rather than the 288
  on the manifest pin. Check it out separately so a workspace pinned elsewhere is untouched.
  The directory **must be named `reqmgmt`**: the Zephyr module name comes from the basename,
  and `ZEPHYR_REQMGMT_MODULE_DIR` is what `doc/CMakeLists.txt` looks for.

```bash
mkdir -p /tmp/req && git -C "$(west topdir)/doc/reqmgmt" \
  worktree add /tmp/req/reqmgmt origin/collab-requirements
REQDIR=/tmp/req/reqmgmt
```

## Run it

```bash
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR="$HOME/zephyr-sdk-1.0.1"

DOCBUILD=/tmp/spdx-docbuild   APPBUILD=/tmp/spdx-appbuild
TWOUT=/tmp/spdx-twister       OUT=/tmp/spdx-3.1
```

### Steps 1–2, from a `collab-safety-spdx-jul5` checkout

```bash
# 1. Documentation -> traceability.json.
#    Doxygen is required (the requirement<->test links come from its XML); Kconfig, the board
#    catalog and the deep external content are not, and skipping them saves the bulk of the
#    build. -W is dropped because that branch predates the current Doxygen's stricter \ref
#    resolution and trips ~276 pre-existing warnings.
cmake -GNinja -B "$DOCBUILD" -S doc \
  -DEXTRA_ZEPHYR_MODULES="$REQDIR" \
  -DSKIP_KCONFIG=ON -DSKIP_BOARD_CATALOG=ON -DSKIP_EXTERNAL_CONTENT=ON \
  -DSPHINXOPTS="-j auto --keep-going -T"
ninja -C "$DOCBUILD" html          # traceability.json lands in $DOCBUILD/html/

# 2. Tests -> twister.json + per-test coverage matrix.
#    mps2/an385, not qemu_x86: with semihosting the per-test gcda is written straight to the
#    host filesystem. Over the console the hex dump gets corrupted and the coverage converter
#    aborts the whole run ("Unable to convert hex data ... list index out of range").
./scripts/twister -p mps2/an385 \
  -T tests/kernel -T tests/arch/interrupt -T tests/lib/multi_heap \
  --coverage-per-test --coverage-tool lcov --outdir "$TWOUT" -j 8
```

### Steps 3–4, from this branch

```bash
# 3. Application build. CONFIG_BUILD_OUTPUT_META makes the build emit zephyr.meta and request
#    the CMake file-based API; `west spdx --init` is no longer needed on this branch.
west build -p always -b m5stack_paper_color/esp32s3/procpu \
  -d "$APPBUILD" samples/modules/lvgl/demos -- -DCONFIG_BUILD_OUTPUT_META=y

# 4. The SBOM.
west spdx -d "$APPBUILD" --spdx-version 3.1 --analyze-includes --include-sdk \
  --requirements-dir "$REQDIR" \
  --traceability "$DOCBUILD/html/traceability.json" \
  --twister-json "$TWOUT/twister.json" \
  --coverage "$TWOUT/coverage/test_matrix.json" \
  -s "$OUT"
```

`--analyze-includes` re-runs the compiler with `-E -H` on every translation unit so the
headers each source pulls in become files in their own right; `--include-sdk` adds the `sdk`
document the toolchain headers among them belong to. Both are what make this a "full" run,
and what makes it slow (a few minutes, dominated by that per-file pass and the license scan).

Any board works; `m5stack_paper_color/esp32s3/procpu` is what the hosted sample uses. Pick one
with a `zephyr,display` chosen node, since the LVGL sample requires it.

### Generate from a checkout at `<topdir>/zephyr`

File names in the zephyr component are relative to the **west topdir**, so the checkout has to
sit at `<topdir>/zephyr` for them to read `zephyr/drivers/display/display_ed2208_gca.c`. Build
from a git worktree and they carry its path instead —
`zephyr/.claude/worktrees/<name>/drivers/display/display_ed2208_gca.c` — for every zephyr
source, plus a second variant on the coverage snippets in `safety.jsonld`. Module and SDK
files are unaffected: they are relative to their own component's base directory.

If a plain checkout is not available, a throwaway workspace works, as long as the module trees
are **real directories**. Symlinking them makes the compiler report realpaths that no longer
sit under the module component's base directory, and `--analyze-includes` then silently drops
every module header — 493 of them for this build. `cp -al` gives real paths for no disk:

```bash
WS=/tmp/zws
mkdir -p "$WS/.west" && printf '[manifest]\npath = zephyr\nfile = west.yml\n' > "$WS/.west/config"
git -C "$(west topdir)/zephyr" worktree add "$WS/zephyr" <branch>
cp -al "$(west topdir)/modules" "$WS/modules"        # hard links, not a symlink
ln -s "$(west topdir)/bootloader" "$WS/bootloader"   # not compiled here, symlink is fine
```

### Running from a git worktree

`west` resolves extension commands through the manifest, so `west spdx` runs the copy of
`scripts/west_commands/spdx.py` in the workspace's `zephyr` project — *not* the worktree you
are standing in, even with `ZEPHYR_BASE` set. Point west at a throwaway local config instead:

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

Both paths are relative to `west topdir`. Without it, `west spdx` rejects the SPDX 3.1 flags
as unknown arguments, which is the tell.

## Expected output

`west spdx` prints what each input contributed — check these before trusting the result:

```
extracted 40 devicetree hardware component(s) from .../edt.pickle
traceability: 529 requirement(s), 76 design(s), 844 test(s)
requirements: loaded 529 requirement(s) from .../reqmgmt
twister: 897 qualified test result(s) from .../twister.json
sources: resolved 190 of 199 implementation symbol(s) to bodies
coverage: indexed 673 test(s) and 108 file(s) of run coverage from .../test_matrix.json
```

Reference run (2026-08-27), 8.5 MB and 14,924 elements over six documents:

| Document | Size | Elements | Imports | Profiles |
| --- | ---: | ---: | ---: | --- |
| `app.jsonld` | 5 KB | 14 | 0 | core, software, simpleLicensing |
| `build.jsonld` | 922 KB | 612 | 1959 | core, software, simpleLicensing, **build**, **hardware** |
| `modules-deps.jsonld` | 143 KB | 273 | 0 | core, software, simpleLicensing |
| `safety.jsonld` | 4.4 MB | 7000 | 28 | core, software |
| `sdk.jsonld` | 74 KB | 158 | 0 | core, software, simpleLicensing |
| `zephyr.jsonld` | 3.1 MB | 6867 | 78 | core, software, simpleLicensing |

They cross-reference each other through `ExternalMap`/`locationHint`, so load them
**together** — drop all six onto the visualizer at once, or use its file picker.

### `build.jsonld` — the Hardware profile

41 `hardware_PhysicalHardware` (the board plus one per enabled devicetree node), 40 `contains`
edges reproducing the devicetree hierarchy, `runsOn` from `zephyr.elf` and `zephyr_pre0.elf`
to the board, and external identifiers on the components: 40 `devicetree-compatible`, 40
`devicetree-path`, 39 `devicetree-binding-type`. Six vendor `Organization` elements are
referenced as `hardware_productAgent`; 28 `build_Build` elements carry `build_buildType`
`urn:zephyrproject.org:build-type:cmake`.

### `zephyr.jsonld` — driver-to-hardware linkage

11 `configures` relationships, each from a driver source to the component for the devicetree
node it instantiates — `i2c_esp32.c` to `/soc/i2c@60013000`, `display_ed2208_gca.c` to
`/mipi_dbi/ed2208_doa@0`. Both `espressif,esp32-spi` instances resolve to `spi_esp32_spim.c`
separately, because the link is keyed on the node's dependency ordinal rather than on its
compatible. They live here (this document owns the driver sources) and point into
`build.jsonld`, arriving as ExternalMap imports — that is what takes this document from 67
imports to 78.

Of the 15 devices the build instantiates, 11 link; the other four are nodes `_is_hardware()`
does not emit a component for — `/mipi_dbi` (a `zephyr,*` abstraction) and the three
`pmic@6e/regulators/*` child-binding nodes, which have no `matching_compat`.

### `safety.jsonld` — coverage-backed traceability

529 `Requirement`, 76 `Specification`, 680 `functionalsafety_RequirementVerification`, 680
`functionalsafety_EvaluationResult`, 427 `functionalsafety_EvidenceRelationship` and 1509
`software_Snippet` — the snippets being the contiguous line ranges each test actually executed
inside an implementation body, which is what makes the traceability "true" rather than merely
declared.

## Known gaps

* **`safety.jsonld` declares only `core, software`.** SPDX 3.1 ships the `functionalsafety_*`
  classes but its `ProfileIdentifierType` enumeration has no `functionalSafety` member, so a
  document cannot claim conformance to the profile whose elements it carries.
* **The safety evidence describes a different tree.** The annotations live on
  `collab-safety-spdx-jul5`, so `traceability.json` and the coverage matrix are generated
  there while everything else describes this branch. `sources.py` resolves implementation
  bodies at the coverage build's commit, so line numbers stay correct, but it is a provenance
  seam. Closing it means porting the ~127 annotation commits onto `main` as well.
* **The board element carries no external identifiers.** `_create_hardware_object()` reads
  `ARCH` and `CMAKE_SYSTEM_PROCESSOR` from `CMakeCache.txt`, but Zephyr sets neither as a
  cache entry, so `target-arch` / `target-processor` are silently dropped. The devicetree
  components are unaffected — their identifiers come from the EDT, not the cache.
* **Vendor ids contain spaces.** `normalize_spdx_name()` only maps `_` to `-`, so five of the
  six vendors yield ids such as `zephyr:vendors/Espressif Systems`, which is not a legal IRI.
* The `west spdx` progress output says "Written SPDX 3.0 JSON-LD" even for 3.1. Cosmetic.
