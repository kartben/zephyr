# Transcribing a datasheet into a register model

How the emulators on this branch were produced, and the rules that made the difference.
Sixty parts were attempted over three batches; every rule below exists because its
absence cost a build cycle or shipped a wrong number. Most of it applies to a human
author too, but it is written so an agent can follow it unattended.

Candidate for folding into `doc/hardware/peripherals/sensor/emulators.rst`.

## Why air-gap the author from the driver

The model is written from the manufacturer datasheet alone, by someone who cannot read
the Zephyr driver, its binding, its tests, or any third-party driver for the part. That
is the whole point: where the model and the driver disagree, one of them is wrong, and
the disagreement is evidence. Let the author see the driver and the test becomes a
tautology that reproduces the driver's bugs.

It worked. Independent transcription found a `stts751` conversion off by a factor of
2.56, a `SENSOR_CHAN_VSHUNT` unit contradiction confirmed twice from different parts, and
an `ist8310` driver reading a register absent from its datasheet.

The isolation must be by construction, not by instruction: give the author a sandbox
directory holding only what it is allowed to see, and it cannot drift.

## The sandbox

One directory per part, outside the Zephyr tree, containing:

    datasheet.pdf         the manufacturer datasheet, if you can pre-fetch it
    emulators.rst         how a register model works
    emul_sensor_regmap.h  the framework API, every field with its doc comment
    sensor_channel.h      just `enum sensor_channel`, with the SI unit of each value
    examples/             four to seven existing models, chosen to span the mechanisms

Pick examples that demonstrate the mechanisms, not the simplest ones. The set that
worked: `p3t1755` (minimal), `tcn75a` (one-shot and shutdown), `bma280` (variants and
selectable range), `lps22hb` (conditions and callbacks), `stts751` (a write callback for
convert-on-any-write), `max44009` (a field split across registers), `tmp451` (read and
write address aliasing, offset binary via a sample callback).

Adding `stts751` and `max44009` between batches measurably reduced the failures whose
cause those two examples demonstrate.

## Datasheet acquisition

Roughly half of vendor sites refuse a plain `curl`: ST, ADI, onsemi, AKM, Memsic,
Microchip and QST all time out or return 403 HTML. Pre-fetch what you can, and tell the
author to search for the rest, verify with `file` that it got a PDF rather than an error
page, and fall back to a distributor mirror hosting the manufacturer's own PDF (Mouser,
DigiKey, Farnell, LCSC). Agents recovered every datasheet this way, including parts whose
vendor site blocked scripted access entirely.

## The rules

Each is stated as it appeared in the prompt, with the failure that produced it.

### 1. Transcribe the entire register map

Every address the device answers to, its datasheet name, reset value, width, read-only
status and writable bits. An address missing from the table fails the transfer with
`-EIO`, so an incomplete table shows up as a broken driver rather than a silently wrong
one. Reserved addresses the device does respond to belong in the table.

*From:* `cht8315` omitted the Device ID register at `0xFF` because the datasheet
documents the register but never states its value; the driver read it and failed init.

### 2. Model every documented data-ready flag

If the datasheet documents a data-ready, conversion-complete or status flag, model it with
`.ready` on the channel, `.clear_on_read`, `.read_clears` or a callback. Drivers poll
these flags, sometimes with no timeout, so a flag that is never set hangs the driver
forever rather than failing a comparison.

*From:* `ist8310` reported "Data not ready" and blocked; `ak8975` and `bmg160` read zero.

Make the author report a `models_data_ready` boolean. As a reported field this reached
12 of 15 compliance; the same instruction as prose was skipped three times.

### 3. Add a write callback for convert-on-wake

`convert_on_write` fires only on a bit written as **one**. A part that converts
continuously whenever it is not in shutdown does not re-convert when the driver *clears*
the shutdown bit, and a part that converts on a mode change does not either. Detect the
transition in a `.write` callback and call `emul_sensor_regmap_convert()`.

*From:* `veml7700` read zero; `ina219` hung the entire suite twice, because its driver
polls a conversion-ready bit that a configuration write had just cleared.

### 4. No register wider than four bytes

Registers live in a `uint32_t`. The framework rejects the whole model at init if any entry
is wider, and **the rejection is silent** — the emulator simply does not exist and the
symptom is a suite-wide hang naming nothing. A part with 48-bit or 56-bit accumulators is
not modelable.

*From:* `pac194x`, three build cycles to find.

### 5. Never map a measurement onto an unrelated channel

If no `enum sensor_channel` value matches the physical quantity, leave the measurement out
and record it as unsupported. A channel the driver does not decode fails with `-EIO` from
the decoder, which looks like a bus error and is not one.

*From:* `veml7700`'s WHITE output squeezed into `SENSOR_CHAN_LIGHT`, plus five batch-2
models exposing channels their drivers do not implement.

### 6. Watch the base of printed values

A default shown as `10` in a table with no `0x` prefix is usually hex. Datasheets mix
conventions between their register summary and their bit-field tables.

*From:* `ist8310`'s device ID transcribed as decimal `10` instead of `0x10`.

### 7. Reset values are for an instantaneous device

A datasheet-faithful "not ready yet after power-on" reset value is wrong here.
Conversions complete instantly in this framework, so nothing ever clears such a flag, and
a driver polling it at init hangs before any test runs. Reset those flags to their ready
state and say why in a comment.

*From:* `max17055` (`FSTAT.DNR`) and `th02` (`STATUS./RDY`) chose this independently, and
both hung.

## Refuse early: the unmodelable taxonomy

Tell the author to set a blocked flag and explain, rather than fake a model. Six reasons,
in the order they are worth checking:

1. Raw counts with no datasheet constant to an SI unit, or needing OTP or calibration
   coefficients — BMM350, BMC150, BH1790, TSL2540.
2. A command or opcode protocol with no register pointer, or a per-word CRC — WSEN-HIDS
   and the Sensirion family.
3. A measurement that is a nonlinear or piecewise function of two or more ADC channels —
   TSL2561, TSL2591.
4. Data delivered only by interrupt, or through another chip's internal I2C master —
   TMD2620, MPU9250.
5. Registers wider than the 32-bit store — PAC194x.
6. No matching `enum sensor_channel` value at all — FDC1004 (capacitance).

Naming 1 to 3 in the prompt doubled the refusal rate and moved those discoveries from an
expensive build hang to a cheap upfront answer. Reasons 4 to 6 still need integration to
find: 4 in particular is invisible from the datasheet, because it is the *driver's* design
that makes the part unmodelable.

## Check statically before you build

Each of these took a full build-and-test cycle to discover the first time, and costs
seconds as a grep:

* any register wider than four bytes (rule 4)
* any `reset` / `write_mask` / `self_clear` written as bare decimal — then verify, since
  some genuinely are decimal, such as the SBS defaults in minutes
* any register the driver reads or writes that the model does not define
* whether the compatible already has an emulator **anywhere** under `drivers/`, not just
  under `drivers/sensor/` — `sbs,sbs-gauge` is served by two drivers in different
  subsystems and only the fuel_gauge one has an emulator
* required devicetree properties, resolved through the binding's `include:` chain, not
  just the leaf binding

## The differential test is the oracle

Run the model against the real driver in `tests/drivers/sensor/regmap`. That is what finds
errors. A second author re-reading the datasheet to audit the transcription changed 1 model
in 15, and that change was a register-permission nit; it passed a model carrying a real
numeric error as clean. Skip the audit and spend the budget on landing and running.

Classify failures from the assertion output before reading any code:

| `got/expected` | meaning |
|---|---|
| exact power of two or ten | wrong LSB by that factor |
| exactly 1000 | check the channel's documented SI unit against what the drivers emit |
| negative | sign or offset error |
| `got 0` | the model never converted — rule 3 |
| within a few epsilon of 1 | quantisation, not a defect; suspect the tolerance |
| `-EIO` from the decoder | channel the driver does not implement — rule 5 |
| `-EIO` at init, or a hang | incomplete table, unset ready flag, or rule 4 |

## What this cannot reach

There is no vocabulary for attributes, alerts, thresholds or interrupts, so that code is
invisible to this approach by construction. On the committed set `ina2xx_attr.c` is 0/68,
`ina2xx_trigger.c` 0/19 and `tmp1075_trigger.c` 0/14. A part whose attributes or triggers
need testing wants a hand-written emulator instead.

## The prompt

Substitute `<NAME>`, `<KIND>`, `<COMPAT>`, `<DIR>` and `<BASE>`. The isolation paragraph
must name the actual tree path so the author cannot wander into it.

```
You are writing a Zephyr sensor emulator for the <NAME> (<KIND>), devicetree compatible
"<COMPAT>", purely from its datasheet.

HARD ISOLATION RULE - this is the point of the exercise, and violating it destroys the
result: you must NOT read, grep, search, list or open ANY file under <ZEPHYR TREE>. In
particular the Zephyr driver for this part is off limits, as are its devicetree binding,
its Kconfig and its tests. Do not fetch the driver from the web either: no
zephyrproject-rtos, no source browsers, no vendor SDK driver code, no Linux kernel
driver, no Arduino or CircuitPython library for this part. The emulator must be an
INDEPENDENT model built only from the manufacturer datasheet, so that a disagreement
between it and the Zephyr driver is evidence of a real bug in one of them. Everything you
need is inside your sandbox directory. Work only there.

SANDBOX: <DIR>
  emulators.rst          how a register model works. READ THIS FIRST, all of it
  emul_sensor_regmap.h   the framework API: every field you may use
  sensor_channel.h       enum sensor_channel, with the SI unit of each value
  examples/              existing models spanning the mechanisms

[If pre-fetched] The datasheet is at <DIR>/datasheet.pdf. Read it with the Read tool
using the pages parameter, at most 20 pages per call.
[Otherwise] Find the current manufacturer datasheet with WebSearch and download it to
<DIR>/datasheet.pdf with a browser User-Agent. Verify with 'file' that it is a PDF and
not an HTML error page. If the vendor site blocks scripted access, try WebFetch on the
PDF URL, or a distributor mirror hosting the manufacturer's own PDF (Mouser, DigiKey,
Farnell, LCSC).

TASK: write <DIR>/<BASE>_emul.c, a complete register model.

1. Transcribe the ENTIRE I2C register map: every address the device answers to, its
   datasheet name, reset value, width, read-only status and writable bits. An unlisted
   address fails the transfer with -EIO, so an incomplete table shows up as a broken
   driver. Reserved addresses the device responds to belong in the table.
2. Describe each measurement as a struct emul_sensor_channel using only values from
   sensor_channel.h, in the SI unit its doc comment states. Convert from datasheet units:
   g = 9.80665 for m/s^2, dps to rad/s, hPa to kPa, uT to Gauss, mA to A.
3. Use .select and .variants where resolution or full scale depends on a configuration
   field.

RULES. Each of these caused a real failure:

(a) DATA READY. If the datasheet documents a data-ready, conversion-complete or status
    flag, you MUST model it, via .ready on the channel, .clear_on_read, .read_clears or a
    callback. Drivers poll these, sometimes with no timeout, so a flag that is never set
    hangs the driver forever. Report models_data_ready.

(b) CONVERT ON WAKE. convert_on_write fires only on a bit written as ONE. A part that
    converts continuously whenever it is not in shutdown does NOT re-convert when the
    driver CLEARS the shutdown bit, and a part that converts on a mode change does not
    either. Add a .write callback calling emul_sensor_regmap_convert() on that
    transition, as examples/stts751_emul.c does. Report models_convert_on_wake.

(c) NO REGISTER WIDER THAN 4 BYTES. Registers are stored in a uint32_t and the framework
    rejects the whole model at init, silently, if any entry is wider. If the part has
    48-bit or 56-bit accumulators it is NOT modelable: set ok=false and say so.

(d) NEVER MAP A MEASUREMENT ONTO AN UNRELATED CHANNEL. If no enum sensor_channel value
    matches the physical quantity, leave it out and record it in unsupported.

(e) RESET VALUES ARE FOR AN INSTANTANEOUS DEVICE. Conversions complete immediately here,
    so a flag whose datasheet reset means "not ready yet after power-on" must reset to its
    READY state instead; nothing would ever clear it and a driver polling it at init would
    hang. Say why in a comment.

FORMAT
 - Zephyr C style: 8-column hard tabs, 100 column limit, /* */ comments only.
 - The two SPDX lines as in the examples, then "#define DT_DRV_COMPAT <compat_token>" and
   the framework include.
 - Above the register table, one comment with the datasheet document number, revision and
   register-map section, plus the URL. Copy identifiers exactly as printed. Watch the base
   of printed values: a default shown as "10" with no 0x prefix is usually hex.
 - An enum or #defines for register addresses, named as the datasheet names them.
 - Comments only where a number is not self-evident. Do not narrate. No emoji.
 - End with EMUL_SENSOR_REGMAP_DEFINE(...).

NOT MODELABLE. Set ok=false and explain rather than faking a model:
 - a command or opcode protocol with no register pointer, or a per-word CRC
 - a part needing calibration or OTP coefficients to reach a physical value, where no
   datasheet constant gives units per LSB
 - a measurement that is a nonlinear or piecewise function of two or more ADC channels
 - a part reached through another chip's internal I2C master, or needing bank switching
 - registers wider than 4 bytes (rule c)

Do not build anything. Correctness of the numbers is the whole deliverable. Recompute
every LSB from the datasheet and re-check every reset value against the register table
before finishing.
```

Have the author return structured output: `ok`, `blocked_reason`, `datasheet_rev`,
`datasheet_url`, `num_regs`, `channels`, `models_data_ready`, `models_convert_on_wake`,
`unsupported`, `framework_gaps`, `notes`.

The boolean fields are not bookkeeping. A rule stated as prose gets skipped; the same
rule as a field the author must answer for reached 12 of 15 compliance. `framework_gaps`
is the most valuable field of the lot: across sixty parts it collected 177 distinct things
the model could not express, which is the real specification for what the framework should
grow next.
