.. SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
.. SPDX-License-Identifier: Apache-2.0

.. _sensor-emulators:

Emulators
#########

Sensor emulators implement the sensor backend API on an emulated I2C bus. A test injects a
physical value, the real driver reads the emulated device, and the test checks what the driver
decoded. :kconfig:option:`CONFIG_EMUL_SENSOR_REGMAP` enables the register table models.

A model is data transcribed from the datasheet: which registers exist, and where each
measurement sits in them. The framework supplies the bus protocol and the backend API.

Writing a register model
************************

Read the datasheet independently of the driver. A model written from the driver reproduces the
driver's mistakes and the test proves nothing. P3T1755's complete model is:

.. literalinclude:: ../../../../drivers/sensor/nxp/p3t1755/p3t1755_emul.c
   :language: c
   :start-at: enum {

:c:macro:`EMUL_SENSOR_REGMAP_DEFINE` registers every enabled I2C instance of ``DT_DRV_COMPAT``.
Add the source to the driver's CMake file under ``CONFIG_EMUL_SENSOR_REGMAP``. There is no
generator.

Register rows carry the address, a diagnostic name, permissions and optional behavior:

* ``bytes`` overrides the device's ``reg_bytes`` (default one). **A register may not exceed four
  bytes**; the whole model is rejected at initialization if one does.
* ``EMUL_SENSOR_REG_RO`` ignores writes; otherwise ``write_mask`` limits writable bits, and zero
  means all of them. Unlisted addresses return ``-EIO``.
* ``clear_on_read`` clears bits after the last byte of the register is read. ``read_clears``
  clears bits in a different register, such as an output's data-ready flag.
* ``self_clear`` clears command bits after processing. ``reset_on_write`` restores reset values.
* ``convert_on_write`` converts the retained inputs, including in standby. ``requires_standby``
  accepts the command only from the device's disabled state.

Channel rows give the first output register, signedness, bit width and position, SI units per
LSB, and offset. Use the unit each :c:enum:`sensor_channel` value documents. ``min`` and ``max``
give the measurement range; both zero derives it from the field.

``select`` chooses an entry of ``variants`` for a configurable range or resolution, indexed by
the raw value of the selecting field. ``whole_word`` clears bits outside the sample field.
``ready`` sets status bits after sampling and ``overrun`` marks replaced unread samples.
``disabled`` suppresses sampling while a condition holds, and ``block_update`` holds unread
samples.

``big_endian`` selects MSB-first transfers. ``byte_addressed`` makes multi-byte entries occupy
consecutive byte addresses. ``fixed_pointer`` retains the selected register across STOP.
``addr_ignore`` masks off bits of the pointer byte that are not part of the address.

Optional ``read``, ``write`` and ``sample`` callbacks cover behavior the table cannot express.
They run under the instance mutex and must not call a driver. Prefer the table: a model that
needs a callback is harder to review, and the callback is where mistakes hide.

Behavior you have to model
**************************

Conversions here are instantaneous. That single difference from real silicon causes most of the
mistakes made when writing a model:

Data-ready flags
   If the datasheet documents a data-ready or conversion-complete flag, model it, through
   ``ready``, ``clear_on_read``, ``read_clears`` or a callback. Drivers poll these flags, and
   several poll them with no timeout, so a flag that is never set does not fail the test: it
   hangs the driver.

Flags that power up "busy"
   A flag whose datasheet reset value means *not ready yet* must reset to its **ready** state
   instead. Nothing would ever clear it, and a driver polling it during initialization hangs
   before any test runs.

Leaving shutdown
   ``convert_on_write`` fires only on a bit written as **one**. A device that converts
   continuously whenever it is not shut down does *not* re-convert when the driver **clears** the
   shutdown bit. Detect that transition in a ``write`` callback and call
   :c:func:`emul_sensor_regmap_convert`.

Complete register tables
   List every address the device answers to, including reserved ones. An unlisted address fails
   the transfer, which surfaces as a driver error rather than as a wrong value.

What cannot be modeled
**********************

Recognize these early and write a bespoke emulator instead, or none:

* Outputs that are raw counts with no datasheet constant giving units per LSB, or that need
  per-part trim or calibration coefficients.
* Command or opcode protocols with no register pointer, and protocols with a per-word CRC.
* A measurement that is a nonlinear or piecewise function of two or more converter channels.
* Data delivered only by an interrupt, or through another device's internal I2C master.
* Registers wider than four bytes.
* A quantity with no matching :c:enum:`sensor_channel` value.

Attributes, alerts, thresholds and triggers have no representation here, so the driver code
implementing them is unreachable by this approach. Storing a configuration bit does not mean its
feature is simulated. SPI and I3C are not supported.

When a test fails
*****************

**Suspect the model first.** Across sixty parts modeled this way, disagreements between a model
and a driver were roughly six times more likely to be the model's fault than the driver's. A
failure is not evidence of a driver bug until the datasheet says so, and reporting one as such
wastes a maintainer's time.

The ratio of the decoded value to the injected value identifies most causes without reading any
code:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Observed
     - Usual cause
   * - An exact power of two or ten
     - Units per LSB wrong by that factor
   * - Exactly 1000
     - The channel's documented SI unit disagrees with what drivers emit
   * - Negative
     - Sign or offset error, or offset binary read as two's complement
   * - Zero
     - The model never converted; see *Leaving shutdown*
   * - Within a few epsilon of one
     - Quantization, not a defect
   * - ``-EIO`` from the decoder
     - A channel the driver does not implement
   * - ``-EIO`` at initialization, or a hang
     - Incomplete table, or a status flag that is never set

Testing
*******

``tests/drivers/build_all/sensor`` discovers every emulator through the devicetree and exercises
it with no per-model code: it injects values across each channel's range and checks what the
driver decodes. Adding a model means adding one source file and one devicetree node.

``tests/drivers/sensor/regmap`` supplies that devicetree and adds deeper tests for a few models
where attributes or nonvolatile behavior are worth asserting; those are opt-in, not a
requirement for a new model. ``tests/drivers/sensor/emul_regmap`` tests the framework itself
against a table defined by the test, without a driver.

.. code-block:: console

   west twister -p native_sim -T tests/drivers/sensor/regmap -T tests/drivers/sensor/emul_regmap -i

Add ``--coverage`` for coverage reports.

API reference
*************

.. doxygengroup:: emul_sensor_regmap
