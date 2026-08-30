<!--
SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
SPDX-License-Identifier: Apache-2.0
-->

# Reproducing the SPDX 3.1 demo SBOM

Branch-local notes for regenerating the "Zephyr RTOS (SPDX 3.1)" sample used by the
[SPDX 3 visualizer](https://kartben.github.io/spdx3_viz/#s=zephyr-experimental).

This branch is `collab-safety` — the requirement annotation sweep and its sphinx-needs
traceability tooling — plus three pieces of `west spdx` work, so one SBOM carries all of
them and every input describes **the same tree**:

| Commits | What they add |
| --- | --- |
| 4, from `spdx_3.1_hw_profile` | The SPDX 3.1 **Hardware** profile: a `hardware_PhysicalHardware` for the board, one per enabled binding-backed devicetree node, `contains` edges mirroring the devicetree hierarchy, vendors resolved from `vendor-prefixes.txt`, and a Zephyr-controlled namespace for custom identifiers. |
| 1 | **Driver linkage**: the `__device_dts_ord_<N>` symbols `DEVICE_DT_DEFINE()` emits, read out of the linker map, become `configures` edges from each driver source to the node it instantiates. |
| 19 | The **FunctionalSafety** engine: coverage-backed requirement traceability emitted into a standalone `safety.jsonld`. |
| 10, from `west-spdx-ntia-compliance` | The **SBOM minimum elements**: a `software_Sbom` root per document, package checksums, suppliers and originators, declared licenses, and an optional `sbom:` section in `zephyr/module.yml`. |
| 5, from `spdx-module-vex` | **VEX statements** a module declares in `zephyr/module.yml`, plus the `scripts/tests/zspdx` unit suite. |

The Hardware profile and the `Requirement` class are a no-op for `--spdx-version 2.3` / `3.0`:
neither existed before SPDX 3.1. The minimum elements and the VEX statements are emitted for
every version.

## The three generated inputs

The safety graph is *coverage-backed*: it does not merely record that a test claims to verify
a requirement, it checks the test actually executed the implementing code. That needs three
inputs, none of which come from the application build:

| Input | Produced by | Feeds |
| --- | --- | --- |
| `needs.json` | the documentation build (sphinx-needs + Doxygen) | `Requirement`, `Specification`, the requirement↔test↔implementation graph |
| `twister.json` | a twister run | per-test pass/fail → `functionalsafety_EvaluationResult`, and the `evidence:` verdict |
| `test_matrix.json` | the same run, with `--coverage-per-test` | per-test covered line ranges → `software_Snippet` + `EvidenceRelationship`, and the `adequacy:` verdict |

Miss one and the safety document degrades quietly rather than failing, so check the counts
`west spdx` prints (see [Expected output](#expected-output)).

`needs.json` is the sphinx-needs export the docs build writes to `<build>/html/`; the
`zephyr.requirement_traceability` extension fills it from the Doxygen XML, so it carries one
`req` need per StrictDoc requirement, one `test` need per `@verifies` symbol, one `impl` need
per `@satisfies` symbol and one `design_need` per `design` directive. `west spdx` reads it
with the same classification `doc/_scripts/gen_traceability_report.py` uses, so the SBOM
adjudicates exactly the graph the documentation's traceability views show.

## Prerequisites

* Zephyr SDK with `xtensa-espressif_esp32s3` (the app) and `arm-zephyr-eabi` (the tests).
* The workspace venv: `west`, `spdx-tools`, `spdx-python-model`, plus `strictdoc>=0.25.1`,
  `sphinx-needs` and Sphinx for the doc build (`pip install -r doc/requirements.txt`).
* `lcov` on the host — `--coverage-per-test` refuses to run without it.
* Doxygen (1.18 works).
* `reqmgmt` from the **`kernel-requirements`** branch, which is what this branch's `west.yml`
  pins — 580 software plus 39 system requirements. Check it out separately so a workspace
  pinned elsewhere is untouched. The directory **must be named `reqmgmt`**: the Zephyr module
  name comes from the basename, and `ZEPHYR_REQMGMT_MODULE_DIR` is what `doc/CMakeLists.txt`
  looks for.

```bash
mkdir -p /tmp/req && git -C "$(west topdir)/doc/reqmgmt" \
  worktree add /tmp/req/reqmgmt origin/kernel-requirements
REQDIR=/tmp/req/reqmgmt
```

## Run it

```bash
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR="$HOME/zephyr-sdk-1.0.1"

DOCBUILD=/tmp/spdx-docbuild   APPBUILD=/tmp/spdx-appbuild
TWOUT=/tmp/spdx-twister       OUT=/tmp/spdx-3.1
```

```bash
# 1. Documentation -> needs.json.
#    Doxygen is required (the requirement<->test links come from its XML); Kconfig and the
#    external board/sample content are not, and skipping them saves the bulk of the build.
#    Those switches are Sphinx tags now, so they go through SPHINXOPTS_EXTRA. -W is dropped:
#    the tests/ and kernel/ trees the doxyfile now feeds Doxygen trip pre-existing warnings.
cmake -GNinja -B "$DOCBUILD" -S doc \
  -DEXTRA_ZEPHYR_MODULES="$REQDIR" \
  -DSPHINXOPTS="-j auto --keep-going -T" \
  -DSPHINXOPTS_EXTRA="-t skip_kconfig -t skip_external_content" \
  -DDT_TURBO_MODE=1 -DHW_FEATURES_TURBO_MODE=1
ninja -C "$DOCBUILD" html          # needs.json lands in $DOCBUILD/html/

# 2. Tests -> twister.json + per-test coverage matrix. The scope is what
#    zephyr.doxyfile.in feeds Doxygen, so every annotated test is measured.
#    mps2/an385, not qemu_x86: with semihosting the per-test gcda is written straight to the
#    host filesystem. Over the console the hex dump gets corrupted and the coverage converter
#    aborts the whole run ("Unable to convert hex data ... list index out of range").
./scripts/twister -p mps2/an385 \
  -T tests/kernel -T tests/arch -T tests/lib/multi_heap \
  -T tests/lib/devicetree/devices -T tests/subsys/logging \
  --coverage-per-test --coverage-tool lcov --outdir "$TWOUT" -j 8

# 3. Application build. CONFIG_BUILD_OUTPUT_META makes the build emit zephyr.meta and request
#    the CMake file-based API; `west spdx --init` is no longer needed on this branch.
west build -p always -b m5stack_paper_color/esp32s3/procpu \
  -d "$APPBUILD" samples/modules/lvgl/demos -- -DCONFIG_BUILD_OUTPUT_META=y

# 4. The SBOM.
west spdx -d "$APPBUILD" --spdx-version 3.1 --analyze-includes --include-sdk \
  --supplier "The Zephyr Project" \
  --requirements-dir "$REQDIR" \
  --traceability "$DOCBUILD/html/needs.json" \
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

Step 2 keeps every test build directory (twister skips its runtime artifact cleanup whenever
`--coverage` is on), so budget disk for it. Splitting it with `--subset i/N` into separate
output directories and merging the results afterwards —
`twisterlib.coverage.merge_test_matrices()` for the matrices, and concatenating the
`testsuites` lists for the reports — bounds the peak at one subset.

### Keep the paths consistent

Every component's files are named relative to that component's base directory, and the base
directories come from `ZEPHYR_BASE` and the CMake file-API codemodel. If the two disagree —
on macOS, `/tmp` is a symlink to `/private/tmp`, and CMake resolves the application directory
while leaving `ZEPHYR_BASE` as given — no zephyr source matches the zephyr component's base
any more. The build still succeeds and the SBOM is still written; it just silently loses
almost every zephyr source, which takes `zephyr.jsonld` from 6867 elements to 5139 and drops
all 11 driver `configures` links, because the driver file is no longer in the graph to link
from. Use the resolved path (`/private/tmp/...`, or anywhere not behind a symlink) for the
workspace, `ZEPHYR_BASE` and the build directory alike, and check
`paths.source` in `<build>/.cmake/api/v1/reply/codemodel-v2-*.json` against `ZEPHYR_BASE` in
`CMakeCache.txt` if the counts look thin.

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
git -C "$(west topdir)/doc/reqmgmt" worktree add "$WS/doc/reqmgmt" origin/kernel-requirements
```

The `reqmgmt` worktree is not optional there: `CONFIG_BUILD_OUTPUT_META` walks every project
in the manifest to record its revision, and a missing one fails the build.

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

`west spdx` prints what each input contributed. Check these before trusting the result:

```
extracted 40 devicetree hardware component(s) from .../edt.pickle
traceability: 619 requirement(s), 77 design(s), 883 test(s), 259 implementation symbol(s)
requirements: loaded 619 requirement(s) from .../reqmgmt
twister: 1144 qualified test result(s) from .../twister.json
sources: resolved 193 of 259 implementation symbol(s) to bodies
coverage: indexed 749 test(s) and 128 file(s) of run coverage from .../test_matrix.json
```

Reference run (2026-08-30), 9.7 MB and 15,550 elements over six documents:

| Document | Size | Elements | Imports | Profiles |
| --- | ---: | ---: | ---: | --- |
| `app.jsonld` | 6 KB | 15 | 0 | core, software, simpleLicensing |
| `build.jsonld` | 946 KB | 613 | 1959 | core, software, simpleLicensing, **build**, **hardware** |
| `modules-deps.jsonld` | 160 KB | 275 | 0 | core, software, simpleLicensing |
| `safety.jsonld` | 5.2 MB | 7554 | 26 | core, software |
| `sdk.jsonld` | 80 KB | 159 | 0 | core, software, simpleLicensing |
| `zephyr.jsonld` | 3.3 MB | 6934 | 78 | core, software, simpleLicensing |

They cross-reference each other through `ExternalMap`/`locationHint`, so load them
**together**: drop all six onto the visualizer at once, or use its file picker.

### `build.jsonld` — the Hardware profile

41 `hardware_PhysicalHardware` (the board plus one per enabled devicetree node), 40 `contains`
edges reproducing the devicetree hierarchy, `runsOn` from `zephyr.elf` and `zephyr_pre0.elf`
to the board, and external identifiers on the components: 40 `devicetree-compatible`, 40
`devicetree-path`, 39 `devicetree-binding-type`. Vendor `Organization` elements are referenced
as `hardware_productAgent`; 28 `build_Build` elements carry `build_buildType`
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

Seeing 0 `configures` and a `zephyr-sources` package with a few dozen files is the tell that
the build's paths did not agree with `ZEPHYR_BASE` — see [Keep the paths
consistent](#keep-the-paths-consistent).

### Every document — the SBOM minimum elements

Five of the six documents (all but `safety.jsonld`, which the FunctionalSafety serializer
writes on its own path) are rooted in a `software_Sbom` of `software_sbomType` "build", whose
single `rootElement` is the package that document is about. 66 `dependsOn` relationships
relate each module's `-sources` package to `zephyr-sources`, 134 packages carry a
`software_packageUrl`, and the 29 build outputs carry a package-level checksum. 164 of the 165
packages have a supplier and a version, the build outputs inheriting the application's.

`--supplier` is what gives the application and the build outputs theirs. Without it they fall
back to the application's git remote, and then to `NOASSERTION`.

**No module declares VEX statements yet.** The serialization is in (see
`scripts/tests/zspdx/test_vulnerability_assessments.py`), but no `zephyr/module.yml` in the
manifest carries a `security: vulnerability-assessments:` block, so a real run emits none and
the sample shows no `security_*` elements.

### `safety.jsonld` — coverage-backed traceability

619 `Requirement` (580 software plus 39 system, told apart by their
`requirement-level:` identifier), 77 `Specification`, 769
`functionalsafety_RequirementVerification`, 769 `functionalsafety_EvaluationResult`, 440
`functionalsafety_EvidenceRelationship` and 1469 `software_Snippet` — the snippets being the
contiguous line ranges each test actually executed inside an implementation body, which is
what makes the traceability "true" rather than merely declared.

Every software requirement carries an `evidence:` and an `adequacy:` verdict. For the run
above, against the [true-traceability
dashboard](https://testing.zephyrproject.org/true-traceability/index.html):

| Verdict | Dashboard | This SBOM |
| --- | ---: | ---: |
| adequacy `true` / `partial` / `broken` | 228 / 4 / 4 | 229 / 4 / 4 |
| adequacy `no-impl` / `unresolved` / `no-cov` / `unattributed` | 229 / 64 / 42 / 9 | 229 / 64 / 41 / 9 |
| evidence `passing` / `skipped` / `failing` | 348 / 7 / 1 | 364 / 12 / 5 |
| evidence `untested` / `no-run` | 154 / 70 | 154 / 45 |

The 25 requirements that differ all have a verifier the dashboard's campaign never ran: it
covers neither `tests/subsys/logging` nor `tests/lib/multi_heap`, whose 71 and 9 annotated
test functions it records with zero instances. On the 491 requirements both campaigns cover,
both verdicts agree for every single one.

## Known gaps

* **`safety.jsonld` declares only `core, software`.** SPDX 3.1 ships the `functionalsafety_*`
  classes but its `ProfileIdentifierType` enumeration has no `functionalSafety` member, so a
  document cannot claim conformance to the profile whose elements it carries.
* **`safety.jsonld` has no `software_Sbom` root.** It is written by the FunctionalSafety
  serializer rather than the path that roots the other five documents, so it still lists its
  39 packages as `SpdxDocument` roots directly and states no SBOM generation context.
* **The board element carries no external identifiers.** `_create_hardware_object()` reads
  `ARCH` and `CMAKE_SYSTEM_PROCESSOR` from `CMakeCache.txt`, but Zephyr sets neither as a
  cache entry, so `target-arch` / `target-processor` are silently dropped. The devicetree
  components are unaffected — their identifiers come from the EDT, not the cache.
* **Vendor ids contain spaces.** `normalize_spdx_name()` only maps `_` to `-`, so five of the
  six vendors yield ids such as `zephyr:vendors/Espressif Systems`, which is not a legal IRI.
* The `west spdx` progress output says "Written SPDX 3.0 JSON-LD" even for 3.1. Cosmetic.
