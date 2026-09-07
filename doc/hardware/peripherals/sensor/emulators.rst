.. SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
.. SPDX-License-Identifier: Apache-2.0

.. _sensor-emulators:

Emulators
#########

Sensor emulators implement the sensor backend API on an emulated I2C bus. Tests inject physical
values, then exercise the real driver through sample fetch or RTIO read and decode.
:kconfig:option:`CONFIG_EMUL_SENSOR_REGMAP` enables the register table models.

Writing a register model
************************

Read the datasheet independently of the driver. List the registers, their reset values,
permissions, and measurement encodings. A mismatch with the driver must be checked against the
datasheet. P3T1755's complete model is:

.. literalinclude:: ../../../../drivers/sensor/nxp/p3t1755/p3t1755_emul.c
   :language: c
   :start-at: enum {

:c:macro:`EMUL_SENSOR_REGMAP_DEFINE` registers every enabled I2C instance of ``DT_DRV_COMPAT``.
Add the source to the sensor's CMake file under ``CONFIG_EMUL_SENSOR_REGMAP``. There is no generator.

Register rows contain the address, diagnostic name, permissions, and optional properties:

* ``bytes`` overrides the device's ``reg_bytes`` (default one byte).
* ``EMUL_SENSOR_REG_RO`` ignores writes. Otherwise, ``write_mask`` limits writable bits;
  zero means all bits are writable. Unlisted addresses return ``-EIO``.
* ``clear_on_read`` clears bits after the last byte of a register is read. ``read_clears`` clears
  bits in another register, such as an output's data-ready flag.
* ``self_clear`` clears command bits after processing. ``reset_on_write`` restores reset values.
* ``convert_on_write`` converts retained inputs, including in standby. ``requires_standby``
  requires the previous configuration to match the device's ``disabled`` condition.

Channel rows give the first output register, signedness, bit width, position, SI units per LSB,
and offset. ``min`` and ``max`` describe the measurement range; both zero use the full encoding.
Use kPa for pressure, m/s^2 for acceleration, and rad/s for angular velocity.

``select`` chooses an entry of ``variants`` for configurable ranges or resolution. Nonzero variant
members override the channel defaults. ``whole_word`` clears bits outside the sample field;
otherwise they are preserved, for example a status bit sharing a data register.

``ready`` sets status bits after sampling and ``overrun`` marks replaced unread samples. Device
and channel ``disabled`` conditions suppress sampling while retaining inputs. ``block_update``
holds unread samples. Conditions compare a masked register value; a zero mask disables the rule.

``big_endian`` selects MSB-first transfers. ``byte_addressed`` makes multi-byte register entries
occupy consecutive byte addresses. ``increment`` optionally controls byte increment with a bit.
``fixed_pointer`` retains the selected register across STOP, as on the temperature sensors.
``addr_ignore`` removes non-address bits from the pointer byte. Word writes commit after the last
byte; split write buffers without a restart belong to the same transaction.

Optional read, write, and sample callbacks handle device-specific behavior such as EEPROM
protection or partial reset. They run under the instance mutex and must not call a driver.

Model boundaries
****************

Injecting an input completes a conversion when enabled. Shutdown retains the input for one-shot
commands. Conversions and commands complete instantaneously, with rounding to the nearest count
and halfway values rounded away from zero. These models cannot validate timing, analog accuracy,
averaging latency, GPIO interrupt delivery, or nonvolatile programming and retention.

The original five models additionally implement:

* LPS22HB: power-down, one-shot, increment control, block update, data-ready/overrun and partial
  software reset. FIFO, differential pressure, offset compensation and filtering are not modeled.
* MPU6050: seven measurement channels, ranges, sleep/standby, reset commands and data-ready
  clearing. FIFO, auxiliary I2C, self-test, motion detection and cycle timing are not modeled.
* P3T1755: signed 12-bit output, shutdown and one-shot. Resolution bits change conversion time,
  not precision.
* TCN75A: 9-12-bit quantization, shutdown and one-shot. Reset thresholds follow diagrams 5-4/5-5;
  the summary table conflicts with them. The binding also records differing hardware observations
  about resolution; the model follows the datasheet.
* TMP116/TMP117: modes, data-ready/alert flags, EEPROM unlock, and TMP117 offset/reset.
  TMP117 is the default; :kconfig:option:`CONFIG_SENSOR_EMUL_TMP116` selects TMP116.

The other models cover register storage, sample encoding, configurable fields and declared status
bits. Storing a configuration bit does not imply its associated feature is simulated. SPI, I3C,
CRC commands, bank switching, thermostat fault queues and I2C general-call reset are not supported.
Datasheet revisions and links are recorded beside each model's register table.

Testing
*******

``tests/drivers/sensor/regmap`` runs independent register checks for the five models, both TMP11X
variants, and the existing generic RTIO test against all 16 models. ``tests/drivers/sensor/emul_regmap``
checks the framework's bus and field semantics. Both suites select ``native_sim`` and ``mps2/an385``
for Twister integration:

.. code-block:: console

   west twister -p native_sim -T tests/drivers/sensor/regmap -T tests/drivers/sensor/emul_regmap -i

The generic test in ``tests/drivers/build_all/sensor`` also discovers these emulators. When adding a
model, add a node to the dedicated suite's ``generic.overlay`` so it is exercised there as well.

Add ``--coverage`` for coverage reports. For initialization-only coverage of the same drivers,
run ``tests/drivers/sensor/regmap`` again with ``-x CONFIG_TEST_SENSOR_REGMAP_BASELINE=y`` and a
separate output directory. This is not a repository-wide historical coverage comparison.

API reference
*************

.. doxygengroup:: emul_sensor_regmap
