# Register map emulators: work in progress

See `DATASHEET-EXTRACTION.md` next to this file for the method and the prompt used to
produce these models.

Scratch notes for the uncommitted half of this branch. **This file and the commit
carrying it are not for upstream** — drop them before submitting anything.

The committed history is deliberately narrow: two driver fixes, a documentation rewrite,
and nine emulators that are **pure register tables with no C callbacks**. Everything here
is parked, in three groups:

1. **Models that need a callback.** They pass, but a callback is where mistakes hide and
   each one deserves review on its own terms: ens160, lm75, lm77, max44009, stts751,
   sx9500, th02, ina3221, tmp451, wsen_pads_2511020213301. Whether the framework should
   grow a declarative form for what these do is the open design question; see the
   `framework_gaps` material below.
2. **Models that fail the differential test**, with the cause diagnosed per model.
3. **Models the framework cannot express at all**, kept only as evidence.

None of them are wired into the test overlay, so the suite stays green with them present.

## How these were produced

Each model was transcribed from the manufacturer datasheet by an agent working in an
isolated sandbox that contained only the datasheet, `emul_sensor_regmap.h`,
`emulators.rst`, `enum sensor_channel` and a few example models. The agent could not
read the Zephyr driver, its binding, its tests, or any third-party driver for the part.
That independence is the point: where the model and the driver disagree, one of them is
wrong, and the disagreement is evidence rather than a tautology.

Two batches were run. Batch 1 used a stronger model and included a second agent that
re-read the datasheet to audit the transcription; batch 2 used a smaller model at medium
effort and dropped the audit stage.

The audit stage is not worth its cost: it changed 1 model in 15, and that change was a
register-permission nit plus a comment reword. It passed a model carrying a real numeric
error (VEML7700, 0.0336 vs 0.0288 lx/count) as "clean". Every error that mattered was
found by running the model against the real driver. Spend the budget on landing and
running, not on re-reading.

## Uncommitted models, and why

`got/expected` ratios below are from the generic test's assertion output; they classify a
failure faster than reading the model does. An exact power of two or ten is a scale error,
a negative ratio is a sign or offset error, `got 0` means the model never converted, and a
ratio within a few epsilon of 1 is quantisation rather than a real defect.

### Needs a data-ready or conversion trigger

* `ist8310` — driver reports `Data not ready`; the model never sets the DRDY bit in
  STAT1. Needs `.ready` on the channels.
* `ak8975` — reads 0. The part converts on a write to CNTL measurement mode; the model
  has no trigger for it. Same shape as the VEML7700 fix already committed.
* `bmg160` — reads 0, same cause.

### Channel not implemented by the driver

The model exposes a channel whose physical quantity is right but which the driver does
not decode, so the test fails with `-EIO` from the decoder rather than a wrong value.
Either the model should drop the channel or the driver should gain it; both are
defensible and the choice is not the emulator author's to make alone.

* `apds9306`, `veml6031` — `SENSOR_CHAN_AMBIENT_LIGHT` (`ch 17`)
* `bh1730` — `ch 19`
* `lsm303dlhc_magn`, `mmc56x3` — `ch 12`

### Scale or sign errors in the model

* `adxl313` — ratio -0.25: a sign error and a factor of four.
* `si7060` — 4% low on `SENSOR_CHAN_AMBIENT_TEMP`.
* `si7210` — ratio exactly 2.000; one side's LSB is out by a factor of two.
* `als31300` — ratio 0.9766 = 125/128.
* `nct75` — ratio -3.65; sign plus scale.
* `lis3mdl`, `lps25hb` — ratio 0.9386 on `ch 13`.

### Essentially correct, failing on tolerance

* `cht8315` — error 6554240 against an epsilon of 6553600. Off by 640 parts in 214
  billion. The model is right; the test's epsilon is derived from the model's own
  quantisation and is a hair too tight.
* `lsm6ds0` — 0.06% out, same character.
* `veml7700` — 0.02% out after the LSB fix.

### Blocked on an API question, not on the model

* `ina219` — ratio exactly 1000. The model follows `sensor.h`, which documents
  `SENSOR_CHAN_VSHUNT` as milli-volts. Every in-tree INA driver emits volts. See the
  findings section; this one needs a decision before the model can be called wrong.

### Unclassified

* `tmp112` — fails with an RTIO `-EIO`, not a value mismatch, and its register table was
  verified by hand as correct (config reset `0x60A0`, `write_mask 0x9FC8` correctly
  excluding read-only R1:R0 and AL, TLOW `0x4B00` = 75 degC, THIGH `0x5000` = 80 degC,
  EM handled through `.select`/`.variants`). The failure is in the fetch path and has not
  been traced.

### Not modelable by this framework

Present here for the record only; neither is wired into any build.

* `mpu9250` — reaches its AK8963 magnetometer through the chip's *internal* I2C master.
  The framework has no notion of a nested bus. The driver busy-waits on `SLV4_DONE`
  forever, hanging the whole suite.
* `pac194x` — 56-bit accumulator registers. `struct emul_sensor_regmap_data` stores
  registers in a `uint32_t`, and init rejects anything wider than four bytes.

Agents correctly refused four more parts outright rather than inventing a model: BMM350
(needs OTP trim coefficients, and prefixes every read with two dummy bytes), WSEN-HIDS
(command-opcode protocol with per-word CRC-8), TSL2561 and TSL2591 (lux is a piecewise
nonlinear function of two ADC channels).

## Findings still to be filed

None of these are emulator bugs. Each was found by running a model against a real driver
and each deserves its own issue or patch.

1. `drivers/sensor/ti/ina219/ina219.c` — `while (!(INA219_CNVR_RDY(status)))` has no
   timeout or retry cap. If the conversion-ready bit never arrives the driver hangs
   forever. Reproduces in seconds against the emulator.
2. `drivers/sensor/tdk/mpu9250/ak8963.c` — `do { ... } while (!(status &
   MPU9250_I2C_MST_STS_SLV4_DONE))`, same problem.
3. `drivers/sensor/ist8310/ist8310.c` — reads and writes register `0x0D` to select 16-bit
   per-axis resolution. That register is absent from the IST8310 datasheet's register
   table (section 6.4.1); the driver is relying on vendor reference-code knowledge. Worth
   at least a comment naming the source.
4. `enum sensor_channel` — `SENSOR_CHAN_VSHUNT` is documented as milli-volts. Both
   in-tree INA drivers emit volts (`INA219_V_SHUNT_MUL = 0.00001`, ina2xx `2500/1000`).
   Two independent air-gapped transcriptions followed the documentation and both
   disagreed with the drivers, so this is the comment being wrong rather than a
   coincidence. One-line fix, but it is an API call.
5. `drivers/sensor/emul_sensor_regmap.c` — `emul_sensor_regmap_init()` returns `-EINVAL`
   silently. The emulator simply does not exist afterwards and the symptom is a
   suite-wide hang with no indication of which model is at fault. A `LOG_ERR` naming the
   register would have saved three build cycles here.
6. `doc/hardware/peripherals/sensor/emulators.rst` — the four-byte limit on a register is
   not documented. `bytes` is described only as "register width in bytes".
7. `tests/drivers/build_all/sensor/src/generic_test.c` — when one device of many is not
   ready the suite hangs rather than failing that device fast. At 45 emulated sensors a
   single bad model costs the entire run and yields a timeout instead of a name.
8. `tests/drivers/sensor/regmap/tests.yaml` — the generic scenario needed its timeout
   raised past the 120 s default once the suite passed roughly 40 devices.

## Open review questions on the committed half

* The INA226 emulator adds a second emulation mechanism to a driver family that already
  has three hand-written emulators under `tests/drivers/sensor/ina2{28,30,37}/src/`, and
  puts it in `drivers/` rather than `tests/`. Consider dropping that commit, or migrating
  the family deliberately.
* `max44009_emul.c` has a callback that is never invoked (0 of 5 lines covered).
* The tree now has two conventions for where an emulator lives, `drivers/<vendor>/<part>/`
  and `tests/drivers/sensor/<part>/src/`. Worth settling before this scales further.

## What the framework cannot reach

Attribute and trigger code is invisible to this approach by construction: there is no
vocabulary for attributes, alerts, thresholds or interrupts. Measured on the committed
set, `ina2xx_attr.c` is 0/68, `ina2xx_trigger.c` 0/19 and `tmp1075_trigger.c` 0/14. A part
that needs those tested wants a hand-written emulator, and `emulators.rst` should say so.

## Batch 3 (20 more, 18 vendors)

Run after batches 1 and 2, with four rules added to the prompt, each traceable to a
specific earlier failure: model any documented data-ready flag; add a `.write` callback
for parts that convert on leaving shutdown; refuse parts with registers wider than four
bytes; never map a measurement onto an unrelated channel. Agents also reported a
`models_data_ready` flag so compliance with the first rule was visible without a build.

Result: 16 written, 4 refused by the agent, 14 wired into the suite (SBS gauge and
TMD2620 dropped, see below), **5 green**. One genuine scale error in fifteen models, down
from roughly four in batch 1.

### Committed

lm77, ens160, th02, max30210, wsen_pads_2511020213301.

### Uncommitted, by cause

* `got 0`, no conversion trigger, i.e. rule (b) not applied: `fxos8700`, `rm3100`,
  `tmp108`. Prose in the prompt was not enough; this needs to be a reported field like
  `models_data_ready` so the agent has to answer for it.
* Channel not decoded by the driver (`-22`): `apds9253` (ch 18), `lm95234` (ch 12),
  `max17055` (ch 66).
* Near miss, 0.016% out at about five epsilon: `fxas21002`.
* Real scale error, roughly 6000x: `s11059`.
* Unclassified: `veml6046`.

### Refused by the agent, correctly

* `bh1790` — photodiode counts, no per-LSB constant to any SI unit.
* `bmc150_magn` — uncompensated counts needing trim registers (section 4.3.2), the
  BMM350 shape.
* `tsl2540` — no counts-to-lux conversion given.
* `fdc1004` — capacitance in picofarads, and `enum sensor_channel` has no capacitance
  value. An API gap, not a datasheet one, and the second of its kind after the missing
  white/clear light channel.

### Dropped at integration

* `sbs,sbs-gauge` — already emulated at `drivers/fuel_gauge/sbs_gauge/emul_sbs_gauge.c`.
  The compatible is served by two drivers in different subsystems and only the fuel_gauge
  one has an emulator, so a duplicate `__emulreg_` symbol breaks the link. Any check for
  "which compatibles are already emulated" that scans one subsystem directory gets this
  wrong; mine did.
* `tmd2620` — `sample_fetch()` blocks on `k_sem_take(&data->data_sem, K_FOREVER)`, given
  only from a GPIO interrupt. The framework has no way to deliver one. Not detectable
  from the datasheet: it is the driver's design, not the hardware, that makes it
  unmodelable.

## The unmodelable taxonomy

Six distinct reasons, worth stating in `emulators.rst` so authors stop early:

1. raw counts with no datasheet constant to SI — BMM350, BMC150, BH1790, TSL2540
2. command/opcode protocol with no register pointer, or per-word CRC — WSEN-HIDS,
   the Sensirion family
3. a measurement that is a nonlinear or piecewise function of two or more ADC channels —
   TSL2561, TSL2591
4. data delivered only by interrupt, or through another chip's internal I2C master —
   TMD2620, MPU9250
5. registers wider than the 32-bit register store — PAC194x
6. no matching `enum sensor_channel` value at all — FDC1004 (capacitance)

Agents catch 1 to 3 by themselves once the prompt names them. 4 to 6 still need
integration to discover.

## Drivers polling a status bit with no timeout

Six now, across three batches. Each is a permanent lockup on real hardware if the bit
never arrives, and each reproduces against the emulator in seconds. Two spin with no
`k_sleep` at all.

| driver | wait |
|---|---|
| `ti/ina219/ina219.c` | `while (!(INA219_CNVR_RDY(status)))` |
| `tdk/mpu9250/ak8963.c` | `do { } while (!(status & ..._SLV4_DONE))` |
| `maxim/max17055/max17055.c` | `while (model_cfg & MODELCFG_REFRESH)` |
| `maxim/max17055/max17055.c` | `while (tmp & FSTAT_DNR)` — no sleep |
| `th02/th02.c` | `while (!is_ready(i2c)) { }` — no sleep, empty body |
| `ams/tmd2620/tmd2620.c` | `k_sem_take(&data->data_sem, K_FOREVER)` |

This is the strongest argument for the framework so far: it turns a class of latent
lockup into a reproducible failure in a few seconds of QEMU.

## One more framework note

A datasheet-faithful "not ready yet after power-on" reset value is **wrong** for this
framework. Conversions are instantaneous, so there is no window during which such a flag
would clear on its own, and a driver polling it at init hangs before any test runs. Both
the MAX17055 (`FSTAT.DNR`) and TH02 (`STATUS./RDY`) models made this choice
independently. `emulators.rst` should say so.
