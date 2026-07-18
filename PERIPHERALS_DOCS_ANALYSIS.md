# Analysis of Zephyr Hardware Peripherals Documentation (`doc/hardware/peripherals/`)

**Scope:** all 85 `.rst` pages under `doc/hardware/peripherals/`, cross-checked against
`include/zephyr/drivers/`, `drivers/*/Kconfig`, `samples/drivers/`, and the doc build
conventions used elsewhere in the tree. Every factual claim below (missing Kconfig
symbols, wrong doxygen groups, broken snippets, etc.) was verified against the current
source tree, not just the docs.

---

## 1. Executive summary

The peripherals documentation is **highly bimodal**. Roughly a dozen pages
(`can/controller.rst`, `pwm.rst`, `led.rst`, `rtc.rst`, `i3c.rst`, `audio/dmic.rst`,
`stepper/index.rst`, `sensor/index.rst`, `w1.rst`, `mspi.rst`, `eeprom/shell.rst`,
`can/shell.rst`) are genuinely excellent — concept, devicetree, worked code examples,
shell transcripts, and API reference all in one place. At the other end, **eight pages
for flagship, everyday peripherals are literal stubs with empty Overview sections**:
ADC, SPI, counter, watchdog, IPM, PCIe, TGPIO, and I2C EEPROM target. Between the two
extremes sits a long tail of "skeleton" pages that exist mainly to host a
`doxygengroup` directive.

Beyond unevenness, the audit found **verified factual defects** (nonexistent Kconfig
symbols in `crc.rst`, the kernel spinlock API embedded in `hwspinlock.rst`, broken C
snippets literal-included into the recommended sensor v2 API page, a DTS example that
won't compile in the stepper docs), **systemic gaps** (11 driver classes with no doc
page at all, 38 doxygen groups never rendered anywhere, near-zero linkage to the 60+
driver samples, ~25 shells undocumented), and **pervasive inconsistencies** (toctree
ordering violates its own sorting rule in 11 places, four different heading spellings
for the devicetree section, ad-hoc ref-label naming).

The root cause is structural: **there is no template or checklist for a peripheral doc
page** — `doc/contribute/documentation/guidelines.rst` covers RST mechanics but never
prescribes what a driver-API page must contain, even though
`doc/develop/api/api_lifecycle.rst` requires "documentation of the API (usage)" from
the *experimental* stage onward. Pages are therefore only as good as their individual
author's initiative, and nothing (CI or review checklist) prevents regressions like
dead Kconfig references or uncompiled example code.

---

## 2. Strengths (what works and should be codified)

1. **A clear "gold standard" already exists in-tree.** The best pages independently
   converged on the same recipe, which can be lifted directly into a template:
   - `can/controller.rst` — theory (bit timing diagram), error-state machine, **four
     worked code examples**, and links to two samples.
   - `pwm.rst` — signal diagram, driver model, `PWM_DT_SPEC_GET` example, two worked
     examples, capture section with callback example.
   - `led.rst` — mandatory-vs-optional API table with `-ENOSYS` semantics, color
     indexing model, four usage examples.
   - `audio/dmic.rst` — key-concepts breakdown of the config struct, numbered
     application flow, buffering code, full shell command documentation.
   - `uart.rst` — the model for multi-mode APIs: explains polling/interrupt/async,
     when to use each, and a warning about mixing them
     (`CONFIG_UART_EXCLUSIVE_API_CALLBACKS`).
   - `can/shell.rst` and `eeprom/shell.rst` — model shell docs: exact enable Kconfigs,
     `zephyr-app-commands` build invocation, real console transcripts, output-decode
     table.
2. **Multi-page subtrees for complex subsystems work well.** `sensor/` (usage split
   into fetch-and-get vs read-and-decode, plus an RFC-2119 SHOULD/MUST checklist for
   driver implementers), `stepper/` (two composition scenarios each with DTS + C), and
   `can/` are the best-organized areas. The "Using X" vs "Implementing X drivers"
   audience split in `sensor/index.rst` is unique in the tree and worth generalizing.
3. **Load-bearing behavioral caveats appear in several pages** and are exactly what
   docs should carry: `retained_mem.rst` mutex/ISR tradeoff, `entropy.rst` "not a
   general RNG" warning, `edac/ibecc.rst` NMI/atomic-only synchronization note,
   `sdhc.rst` "use the SD subsystem, not this API directly" guidance, `dma.rst`
   cache-coherency caveat and channel-state graphviz FSM.
4. **Mechanical hygiene is mostly good.** Heading adornment levels (`#`/`*`/`=`) are
   consistent; every top-level page has a ref label; every `doxygengroup` referenced
   resolves to a real `@defgroup`; 111 of 112 unique `CONFIG_*` references exist
   (the exceptions are the two in `crc.rst`, below).

---

## 3. Verified defects (fix immediately)

| # | Location | Defect |
|---|----------|--------|
| D1 | `crc.rst:17-18` | References `CRC_HW` and `CRC_INIT_PRIORITY` — **neither exists anywhere in the tree** (real symbols: `CONFIG_CRC`, `CONFIG_CRC_HW_HANDLER` in `subsys/crc/Kconfig`). Both also lack the `CONFIG_` prefix every other page uses. |
| D2 | `hwspinlock.rst:12` | Embeds `.. doxygengroup:: spinlock_apis` — the **kernel** spinlock group from `include/zephyr/spinlock.h` — in the middle of the Overview, dumping the entire kernel spinlock API into the page. The correct `hwspinlock_interface` group is already at the end. |
| D3 | `sensor/multiple_temp_polling.c`, `sensor/accel_stream.c` | Literal-included into `read_and_decode.rst` (the page recommending the *new* sensor API), but they are not buildable samples and contain real bugs: bare `return;` inside `int main`, references to an undeclared `dev` variable, `struct sensor_chan_spec` passed as a bare brace initializer (invalid C without a compound-literal cast), iterator passed by value where a pointer is required, dead `k_msleep(1)` outside the loop. (`temp_polling.c` and `tap_count.c` are clean.) |
| D4 | `stepper/individual_controller_driver.rst:13` | DTS example line `stepper_driver = &tmc2209` is missing its trailing semicolon — the overlay fails to compile if copy-pasted. |
| D5 | `sensor/index.rst:1` | File starts with a UTF-8 BOM (`EF BB BF`) — the only file in the tree with this encoding artifact. |
| D6 | `otp/api.rst` | Section order inverted (API Reference before Configuration Options — unique in the tree), heading typo "API implementation Reference", overview sentence missing its terminal period. |
| D7 | `coredump.rst:9` | Typo: "with two types.A COREDUMP_TYPE_MEMCPY" (missing space). |
| D8 | `buzzer.rst:14` | Sentence ends "…through the functions in buzzer.h:" — dangling colon, the promised example never follows. |
| D9 | `stepper/index.rst:121-122` | Stray link target pointing at a Discord channel — link-rot-prone and unusual for upstream docs. |
| D10 | `sdhc.rst:44` | "Related configuration options" list is orphaned under the "Host Controller I/O" section instead of a Configuration Options section. |
| D11 | `sensor/attributes.rst:32,36` | Example bugs: format string `%d.06%d` should be `%d.%06d`, `val2` spuriously multiplied by 1000000 (it is already in millionths), and `sensor_attr_set()` passed the `sensor_value` struct by value where the API takes a pointer. |

---

## 4. Content gaps

### 4.1 Stub pages for flagship peripherals (empty Overview, no config section, no guidance)

`adc.rst` (13 lines), `spi.rst` (13), `counter.rst` (13), `watchdog.rst` (13),
`ipm.rst` (13), `pcie.rst` (13), `tgpio.rst` (20, config-only), `i2c_eeprom_target.rst` (12).

These are among the most-used APIs in Zephyr, and their headers carry the richest
surfaces the docs ignore entirely — e.g. `adc.h` (async reads, sequences,
`adc_dt_spec`), `spi.h` (`spi_transceive_cb/signal`, RTIO iodev), `counter.h`
(alarms, top value, guard periods), `watchdog.h` (a safety-critical setup/feed model
with window semantics). An empty `Overview` heading is worse than none: it signals
abandonment and still passes CI.

### 4.2 Skeleton/thin pages (tautological one-liner overview, nothing else)

`psi5.rst`, `sent.rst`, `dac.rst`, `clock_control.rst` (severely underweight for how
widely used it is), `flash.rst` (the "overview" is only the offset-convention note —
no read/write/erase/page-layout usage for the flagship storage API, no config
section), `eeprom/api.rst`, `otp/api.rst`, `entropy.rst`, `crc.rst`, `usbc_vbus.rst`,
`haptics.rst`, `mbox.rst`, `hwspinlock.rst`, `biometrics.rst`, `auxdisplay.rst`,
`audio/codec.rst`, `audio/dai.rst`, `sensor/triggers.rst`, `audio/index.rst` (hub
with zero orienting glue).

### 4.3 Documented APIs whose major surfaces are absent from prose

- **Async/callback APIs are systematically missing:** `i2c.rst` never mentions
  `i2c_transfer_cb/signal` or RTIO; `smbus.rst` describes Host Notify and SMBALERT#
  conceptually but never mentions the `smbus_*_set_cb` registration APIs; `spi.rst`
  is empty so its async surface is invisible. (`uart.rst` and `mspi.rst` show how to
  do this right.)
- **`gpio.rst`** lists interrupts as a feature but has no
  `gpio_init_callback`/`gpio_add_callback` example — a top use case.
- **`led.rst`** advertises the LED-strip API up front but provides no strip
  example.
- **`rtc.rst` and `dma.rst`** are strongly driver-author-oriented with little
  application-developer guidance (no "read/set time and arm an alarm" example; no
  `dma_config`/`dma_start` walkthrough).
- **`pcie.rst`** covers none of MSI, capabilities, or the endpoint API despite
  `include/zephyr/drivers/pcie/` shipping all of them.
- **`mbox.rst` vs `ipm.rst`:** two overlapping inter-processor signalling APIs,
  neither deprecated (verified: no `@deprecated` in either header), and **neither doc
  page mentions the other** or tells the reader which to pick.

### 4.4 Driver classes with no documentation page anywhere

Verified to have driver directories/headers but no page under `doc/` referencing
their API: **FPGA** (`fpga_interface`), **LED strip** (has its own header and shell;
only piggybacks on `led.rst`'s doxygengroup), **MIPI-DBI / MIPI-DSI** (partially
covered inside `display/index.rst` but with no usage docs), **cellular/modem**
(`cellular_interface`), **PTP clock**, **syscon**, **sip_svc**, **power_domain**,
**pm_cpu_ops**, **tee**, **uaol**. Additionally, 38 `@defgroup`s under
`include/zephyr/drivers/` are never pulled into any doc page — some intentionally
(emulator backends), but others are real user-facing API that simply never renders:
`i3c_transfer_api`, `counter_capture`, `flash_ex_op`*, `mspi_*_api` (3 groups),
`regulator_parent_interface`, `video_pixel_formats`, `syscon_interface`,
`fpga_interface`, `ptp_clock_interface`, `tee_interface`, `cache_external_interface`,
and every `*_interface_ext` vendor-extension group.
(*`flash_ex_op` is nested inside `flash_interface` so it renders, but is never
explained in prose.)

### 4.5 Samples linkage is almost nonexistent

`samples/drivers/` contains ~69 sample directories, and the peripherals docs contain
**4 total `code-sample` references** — only CAN links its own driver sample; `gpio.rst`
links only the basic `blinky`. Every other peripheral with a dedicated sample (adc,
dac, i2c, spi_flash, watchdog, counter, rtc, pwm, uart, video, stepper, w1, memc,
mbox, …) never points readers at it. This is the cheapest large win available: the
`:zephyr:code-sample:` machinery exists and the samples already exist.

### 4.6 Shell documentation coverage: ~9 of ~35

Peripheral classes with a `*_SHELL` Kconfig but **no mention of the shell in their doc
page** (~25): adc, bbram, biometrics, counter, dac, edac, flash, haptics, hwinfo,
i2c, i3c, mdio, pcie, peci/espi, regulator, rtc, sensor (!), smbus, spi, stepper,
uart, tcpc, video, w1, watchdog. Documented today: can, eeprom (dedicated pages);
comparator, gpio, led, pwm, otp, dmic, i2s (inline). The sensor shell — one of the
most feature-rich in the tree — is entirely undocumented.

---

## 5. Inconsistencies

1. **Toctree ordering violates its own rule.** `index.rst:7` says "keep the ToC tree
   sorted based on the titles"; there are 11 pairwise inversions. Worst offenders are
   entries whose *file name* sorts near its neighbors but whose *rendered title* does
   not: `charger.rst` ("Chargers" — filed after CAN), `tgpio.rst` ("Time-aware GPIO"
   — filed near the end), `dac.rst`/`dali.rst`, `gnss.rst`/`gpio.rst`,
   `opamp.rst`/`otp`, `sent.rst`/`spi.rst`, `watchdog.rst`/`wuc.rst`,
   `psi5`/`peci`/`ps2`, `uart`/`usbc_vbus`/`tcpc`.
2. **Configuration section:** heading is "Configuration Options" (majority),
   "Configuration" (comparator, opamp, clock_monitor), "Configuration option"
   (singular — both edac pages), or absent entirely (~15 pages incl. adc, spi,
   counter, watchdog, flash, dma, mbox, charger, fuel_gauge, entropy, regulators).
   Depth is equally uneven: many list only the top-level `CONFIG_<X>` enable symbol
   and omit real options (`dac.rst` omits `CONFIG_DAC_SHELL`; `i2c.rst` omits
   `CONFIG_I2C_SHELL`; `w1.rst` omits `CONFIG_W1_SHELL`).
3. **Devicetree heading spelling:** "Devicetree Configuration" (gpio), "Device Tree
   Configuration" (pwm, led), "Devicetree bindings" (rtc), "Device Tree" (mspi, i3c).
4. **Overview heading:** ~18 pages have no `Overview` section at all and open with
   bare prose (bbram, bc12, buzzer, charger, dali, fuel_gauge, i3c, mspi, regulators,
   sdhc, video, several index pages); 8 have the heading with an empty body.
5. **Ref labels:** dominant `_<name>_api` pattern with drift — bare `_can`/`_eeprom`/
   `_otp` on index pages, `_audio_reference`, `_usb_bc12_api` (extra prefix),
   `_regulator_api` for the plural "Regulators" page, hyphenated `_sensor-using`
   style only in the sensor tree, and two sensor sub-pages with no label at all
   (`device_tree.rst`, `power_management.rst`). Several pages carry redundant double
   labels (`_x_api` plus `_x_api_reference`).
6. **Title style:** mixture of "Expansion (ACRONYM)" (majority), "ACRONYM
   (Expansion)" (GNSS), lowercase parentheticals (auxdisplay), a stability tag in the
   title itself (BC1.2 "(Experimental)"), and singular/plural drift ("Chargers",
   "Steppers" vs "Comparator", "Counter").
7. **API Reference section:** usually one heading; `flash.rst` uses "User API
   Reference" + "Implementation interface API Reference" (a defensible split, but
   unique); `otp/api.rst` inverts the order.
8. **Directory structure:** `otp/` is a multi-page directory with a single child page
   — unjustified vs the one-page norm; `eeprom/`'s split is justified by its shell
   page.

---

## 6. Page-by-page scorecard

**Rich (12):** can/controller, can/shell, pwm, led, rtc, i3c, mspi, w1, audio/dmic,
stepper/index, stepper/integrated_controller_driver, sensor/index (+
sensor/fetch_and_get, sensor/read_and_decode\*, eeprom/shell, memc, bc12,
clock_monitor, edac/ibecc, wuc, gpio, uart — 22 total at "adequate-rich or better").
\*read_and_decode is rich in prose but ships the broken snippets (D3).

**Adequate (≈18):** smbus, i2c, comparator, dma, buzzer, retained_mem, bbram, sdhc,
coredump, hwinfo, regulators, charger, fuel_gauge, tcpc, can/transceiver, audio/i2s,
video, display/index, gnss, sensor/attributes, sensor/device_tree,
stepper/individual_controller_driver.

**Thin (≈20):** dac, clock_control, reset, opamp, haptics, mbox, hwspinlock, peci,
ps2, espi, mdio, dali, psi5, sent, flash, eeprom/api, otp/api, entropy, crc,
usbc_vbus, biometrics, auxdisplay, sensor/channels, sensor/power_management,
sensor/triggers, audio/codec, audio/dai.

**Stub — empty overview (8):** adc, spi, counter, watchdog, ipm, pcie, tgpio,
i2c_eeprom_target.

---

## 7. Improvement plan

### Phase 0 — Correctness fixes (small PRs, immediate)

1. Fix `crc.rst` Kconfig references → `CONFIG_CRC`, `CONFIG_CRC_HW_HANDLER` (D1).
2. Remove the misplaced `spinlock_apis` doxygengroup from `hwspinlock.rst` (D2).
3. Fix or replace the two broken sensor snippets; ideally convert all four local
   `.c` files into buildable samples/tests and `literalinclude` from there, as
   `fetch_and_get.rst` already does with `magn_polling` (D3).
4. Fix the stepper DTS semicolon (D4), BOM (D5), otp/api.rst ordering/typos (D6),
   coredump typo (D7), buzzer dangling colon (D8), Discord link (D9), sdhc orphaned
   config list (D10), attributes example bugs (D11).
5. Re-sort `index.rst` by rendered title per its own instruction (§5.1).

### Phase 1 — Define the standard (one PR, unblocks everything else)

Add a **peripheral documentation template + checklist** to
`doc/contribute/documentation/guidelines.rst` (or a new
`doc/contribute/documentation/driver_docs.rst`), codifying the structure the best
pages already use:

```
.. _<name>_api:

<Expanded Name> (<ACRONYM>)          # title style: Expansion (ACRONYM)
###########################

Overview                              # required, never empty: what it is, key
********                              # concepts, sync vs async modes, related APIs

Devicetree Configuration              # standardized spelling; node + dt-spec example
************************

Basic Operation / Usage Examples      # ≥1 worked, buildable code example
********************************

Shell (if CONFIG_<X>_SHELL exists)    # enable Kconfigs + command transcript
*************************************

Related Samples                       # :zephyr:code-sample: links
***************

Configuration Options                 # standardized plural heading; all user-facing
*********************                 # CONFIG_* incl. shell/async options

API Reference                         # doxygengroup(s), sub-grouped for multi-mode APIs
*************
```

Plus rules: ref label `_<name>_api`; example code must come from buildable
samples/tests via `literalinclude` or be covered by a doc build that compiles
snippets; mention deprecation/overlap relationships (mbox↔ipm); user-facing pages
lead with application usage, driver-implementer material goes in a clearly separated
section (the `sensor/index.rst` "Using / Implementing" split).

### Phase 2 — Kill the stubs (highest user impact)

Rewrite the 8 empty-overview pages against the template, in priority order of usage:
**spi → adc → watchdog → counter → gpio-interrupts gap → ipm (+ mbox
cross-reference) → pcie → tgpio → i2c_eeprom_target**. Each is a self-contained
half-day PR: concept overview, dt-spec example, one or two worked examples (sync +
async where the header has both), shell section, config options. `uart.rst` is the
model for multi-mode APIs; `pwm.rst`/`led.rst` for the general shape.

### Phase 3 — Systematic enrichment (batchable, parallelizable)

1. **Async/callback coverage sweep:** add the missing async/RTIO/callback sections to
   i2c, smbus, spi (done in Phase 2), and audit remaining buses.
2. **Samples linkage sweep:** add a "Related Samples" section to every page whose
   peripheral has a `samples/drivers/<x>` directory (~40 pages, mechanical).
3. **Shell docs sweep:** document the ~25 undocumented shells, starting with the
   high-value ones (sensor, i2c, flash, rtc, regulator, adc, uart); follow the
   `can/shell.rst` transcript style. Inline section for small shells, sub-page for
   large ones.
4. **Thin-page uplift:** flash (top priority — flagship API with no usage docs),
   clock_control, dac, entropy, mbox/ipm rationalization, fuel_gauge/charger
   examples.
5. **Missing pages:** create pages (or explicit "documented elsewhere" pointers) for
   FPGA, LED strip, MIPI-DBI/DSI usage, cellular, PTP clock, syscon, power_domain,
   sip_svc, tee; triage the 38 unreferenced doxygen groups — render the user-facing
   ones (i3c_transfer_api, counter_capture, mspi sub-APIs, regulator_parent,
   video_pixel_formats), consciously exclude emulator/internal ones.

### Phase 4 — Guardrails (prevent regression)

1. **CI lint** (extend `doc/` checks): fail on (a) `:kconfig:option:` references to
   nonexistent symbols — this would have caught D1; (b) `doxygengroup` references to
   nonexistent groups; (c) empty section bodies (heading directly followed by another
   heading); (d) title-underline length mismatches (`sphinx -W` on docutils
   warnings); (e) toctree order vs rendered-title sort in `peripherals/index.rst`.
2. **Compile doc snippets:** move all literal-included example code into buildable
   samples or a `tests/docs/` compile-only target so D3-class rot cannot recur.
3. **Review checklist:** add "docs page updated per driver-doc template" to the new
   driver-API review checklist, aligning practice with the
   `api_lifecycle.rst` requirement that usage documentation exists from the
   experimental stage.
4. **Coverage report:** a small script (the audit queries used here are trivially
   scriptable) listing driver classes without doc pages, unreferenced doxygen groups,
   shells without docs, and samples without links — run periodically or in CI as
   informational output.

### Suggested sequencing and effort

| Phase | Effort | Impact |
|-------|--------|--------|
| 0 — defect fixes | ~1 day total, mergeable as 2–3 small PRs | Removes all known factually-wrong content |
| 1 — template | ~1–2 days incl. review discussion (worth an RFC to the docs WG) | Unblocks consistent contribution at scale |
| 2 — 8 stub rewrites | ~0.5 day each | Directly serves the largest user populations |
| 3 — sweeps | Highly parallelizable; good-first-issue material once the template exists | Long-tail quality + discoverability |
| 4 — guardrails | ~2–3 days | Prevents regression permanently |

Phases 0 and 1 can proceed immediately and independently; Phase 3's mechanical sweeps
(samples linkage, shell docs) are ideal community/good-first-issue campaigns once the
Phase 1 template gives contributors a target to hit.
