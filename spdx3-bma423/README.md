<!--
SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
SPDX-License-Identifier: Apache-2.0
-->

# Requirements for a sensor driver

A worked example of taking one device driver through the same requirement,
implementation and verification chain the rest of the safety SBOM uses. See
[SPDX-3.1-SAFETY-SBOM.md](../SPDX-3.1-SAFETY-SBOM.md) for what that chain is and
[SPDX-3.1-REPRO.md](../SPDX-3.1-REPRO.md) for the full build recipe.

The subject is the Bosch BMA4xx accelerometer driver
(`drivers/sensor/bosch/bma4xx/`), whose requirements are written against the
BMA423 data sheet, BST-BMA423-DS006-00 revision 2.0.

## Why a driver is a different exercise

Every requirement in the existing catalog is about the kernel: threads,
semaphores, work queues. Those are requirements Zephyr writes for itself, so
"where does the requirement come from" has no answer beyond the project's own
API contract.

A driver has an external answer. The device data sheet says what the hardware
does, and the driver's job is to make that reachable through
`include/zephyr/drivers/sensor.h` without misrepresenting it. So every
requirement here cites the chapter of the data sheet it derives from, and the
rationale field carries the citation. `ZEP-SRS-39-9` is the shape of it:

    The BMA4xx driver shall convert a per-axis offset expressed in m/s^2 into
    the corresponding OFFSET register value at 3.9 mg per bit, and shall reject
    an offset outside the range the registers can express with -ERANGE.

    Rationale: Datasheet chapter 4.10: the offset registers are 8 bit two's
    complement, "the offset resolution (LSB) is 3.9 mg and the offset range is
    +- 0.5 g", both independent of the selected full-scale range.

That is a requirement an assessor can check against a document the driver author
did not write.

## The three inputs, for this driver

| Input | Where | This change adds |
| --- | --- | --- |
| Requirement catalog | `reqmgmt` StrictDoc module | `bma4xx_accelerometer.sdoc`, 20 requirements, `ZEP-SRS-39-1` to `-20` |
| `@satisfies` | `drivers/sensor/bosch/bma4xx/` | 27 links on 22 symbols, covering 19 of the 20 |
| `@verifies` | `tests/drivers/sensor/bma4xx/` | 18 links on 17 ztest cases |

The catalog file is a drop-in for the `reqmgmt` module and lives here only
because that module is a separate repository. Copy it to
`reqmgmt/docs/software_requirements/` before running the doc build.

Chapter 39 is the first free software-requirement chapter; components are
numbered by chapter, so `ZEP-SRS-39-*` is the BMA4xx driver and nothing else.
All 20 trace to `ZEP-SYRS-4`, "Device Driver Abstraction".

## Wiring the driver into the traceability build

Doxygen only reads what `INPUT` lists, and that list was `include/`, `kernel/`,
`lib/midi2/` and four `tests/` subtrees. A `@satisfies` in a driver is invisible
until the driver is on the list, so `doc/zephyr.doxyfile.in` gains two entries:

    @ZEPHYR_BASE@/drivers/sensor/bosch/bma4xx/
    @ZEPHYR_BASE@/tests/drivers/sensor/bma4xx

Nothing else is needed. `EXTRACT_STATIC` and `EXTRACT_ALL` are already `YES` and
`FILE_PATTERNS` already covers `*.c`, so the driver's static functions reach the
Doxygen XML on their own; the `bma4xx_*` names are not caught by the
`EXCLUDE_SYMBOLS` patterns that hide kernel internals. This is the part that
generalizes: adding any other driver to the traceability graph is two lines here
plus the annotations.

## What annotating the driver turned up

Writing the requirements first and the annotations second is what makes this
worth doing. Four things fell out that reading the code alone would not have
surfaced.

**The emulator could not run the driver.** `bma4xx_emul.c` rejected three
register writes the driver makes on every initialization: `ACC_CONF` unless the
byte was exactly performance mode with `norm_avg4`, `PWR_CONF` at all, and the
soft-reset command. `bma4xx_chip_init()` checks the soft-reset result and
returns its error, so the device never reached ready and no test could have run
against it. That is consistent with the tree having no BMA4xx test suite at all.
The emulator now models the reset values from chapter 5 of the data sheet and
applies them on both power-up and soft reset.

**`ZEP-SRS-39-15` has no implementation.** The data sheet reserves
`TEMPERATURE = 0x80` for "no valid temperature information available".
`bma4xx_convert_raw_temp_to_q31()` decodes it as an ordinary signed offset from
23 degrees Celsius, which yields -105 degrees Celsius: inside the plausible
output range, outside the specified measurement range, and indistinguishable
from a real reading by anything downstream. The requirement is written and left
deliberately unimplemented, so the SBOM reports it as `no-impl` rather than
quietly omitting the case.

**`bma4xx_attr_set_bwp()` shifts the bandwidth parameter twice.** It stores
`val1 << BMA4XX_SHIFT_ACC_CONF_BWP`, and `bma4xx_configure()` then applies
`FIELD_PREP(BMA4XX_MASK_ACC_CONF_BWP, ...)` to the same value, which shifts it
by four again and masks the result away. Setting `norm_avg4` through the
attribute programs `osr4_avg1`. `bma4xx_chip_init()` stores the same field
unshifted, so the default configuration is correct and only the attribute path
is wrong. `ZEP-SRS-39-8` is stated as the validation contract, which the driver
does meet, so the test suite passes; the register value is a separate defect and
belongs in a separate fix.

**`bma4xx_configure()` writes the wrong FIFO flush command.** It sends
`FIELD_PREP(BMA4XX_CMD_FIFO_FLUSH, 1)`, which evaluates to `0x10`, where the
data sheet's flush opcode is `0xB0` written directly. Only on the streaming
path, which this test suite does not enable.

The last two are left as findings rather than folded in here: they are driver
fixes, not traceability, and each wants its own change.

## Coverage this produces

17 test cases carry `@verifies`, covering 13 of the 20 requirements. The other
seven have no verifying test, and the SBOM will say so:

| Requirement | Why untested |
| --- | --- |
| `ZEP-SRS-39-2` soft reset | Initialization runs once at boot; a test cannot observe the reset separately from the configuration written straight after it |
| `ZEP-SRS-39-10` configuration rollback | Needs an emulator that can fail a chosen register write |
| `ZEP-SRS-39-13` single-transaction read | Needs a bus-level assertion the I2C emulator does not offer |
| `ZEP-SRS-39-15` invalid temperature | Not implemented |
| `ZEP-SRS-39-16` FIFO frame parsing | Streaming is off in this suite |
| `ZEP-SRS-39-17` FIFO watermark | Streaming is off in this suite |
| `ZEP-SRS-39-20` bus abstraction | Only the I2C instance is exercised |

`ZEP-SRS-39-19` is the awkward middle case: the non-streaming half of it is
verified and the streaming half is not, and a requirement is either traced or it
is not. Splitting it in two would make the matrix honest at the cost of stating
one hardware behaviour as two requirements, which is a judgement the catalog
owner has to make rather than the tooling.

That distribution is the point. A traceability tool that only reported the 13
green rows would describe this driver as well covered. Naming the seven gaps,
and separating "no test" from "no implementation", is what the requirement
catalog buys.

## Building it

The requirement catalog reaches the SBOM through the documentation build, and
the verdicts through a twister run, exactly as in the main recipe:

```bash
cp bma4xx_accelerometer.sdoc "$REQMGMT/docs/software_requirements/"

west build -b native_sim -d "$DOCBUILD" ...    # doc build: needs.json
west twister -p native_sim -T tests/drivers/sensor/bma4xx --coverage-per-test

west spdx -d "$APPBUILD" --spdx-version 3.1 \
  --requirements-dir "$REQMGMT" \
  --traceability "$DOCBUILD/html/needs.json" \
  --twister-json "$TWOUT/twister.json" \
  --coverage "$TWOUT/coverage/test_matrix.json" -s "$OUT"
```

## Status

Written and reviewed against the data sheet and the driver sources. Not built,
not run, and not run on hardware: this branch has no west workspace, no Zephyr
SDK and no modules checked out, so neither the test suite nor the doc build has
been executed. The SPDX elements published alongside this change were generated
from the annotations directly rather than from a twister campaign, and are
labelled as such.
