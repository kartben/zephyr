.. SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
.. SPDX-License-Identifier: Apache-2.0

.. _sensor_regmap_emulators:

Register table sensor emulators
##############################

Enable :kconfig:option:`CONFIG_SENSOR_EMUL_REGMAP` with sensor nodes on a
:dtcompatible:`zephyr,i2c-emul-controller` bus. The models implement the sensor emulator backend,
so tests inject physical values through :c:func:`emul_sensor_backend_set_channel` and exercise
unmodified sensor APIs. ``ti,tmp11x`` models TMP117 by default;
:kconfig:option:`CONFIG_SENSOR_EMUL_TMP116` selects TMP116.

Writing a model
***************

Start from the datasheet, independently of the driver. Define register addresses and a table
of byte widths, reset values, writable bits, and bits cleared by reading. For example, P3T1755's
entire register map is:

.. literalinclude:: ../../../drivers/sensor/emul/p3t1755.c
   :language: c
   :start-at: enum { TEMP
   :end-at: };

.. literalinclude:: ../../../drivers/sensor/emul/p3t1755.c
   :language: c
   :start-at: static const struct emul_regmap_register
   :end-at: };

Unlisted addresses fail with ``-EIO``. A zero write mask makes a register read-only; writes to
masked bits are ignored. Multi-byte values use the model's byte order. ``byte_addressed`` selects
consecutive byte addresses (LPS22HB and MPU6050); otherwise each pointer selects one complete
register (the temperature sensors). The pointer survives STOP. Consecutive write buffers without
a restart form one write transaction. Word writes commit after their last byte.

Channel entries describe the physical units per signed count, offset, range, and left shift.
Use the sensor API's units, including kPa for pressure and rad/s for angular velocity. Optional
callbacks implement conversion gating, status flags, and commands. They run under the instance
mutex; they must not call a driver. Add a source entry in ``drivers/sensor/emul/CMakeLists.txt``
and instantiate it with ``EMUL_REGMAP_DT_INST_DEFINE``. No generator or additional language is
required. The header is internal while the model interface is experimental.

Model boundaries
****************

Inputs represent an ideal physical stimulus. Injecting a value completes a conversion when the
sensor is enabled; in shutdown the input is retained for the next one-shot command. Conversion
and command completion are instantaneous. Sample encoding rounds to the nearest count, with
halfway values rounded away from zero. Consequently these models cannot validate conversion
waits, sampling rates, averaging latency, or analog accuracy.

* LPS22HB: register map, pressure/temperature samples, power-down, one-shot, increment control,
  block data update, data-ready/overrun flags, and software reset. FIFO, differential pressure,
  offset compensation, and filtering are not simulated.
* MPU6050: sample-rate and range registers, all seven measurement channels, sleep/standby,
  reset commands, and data-ready clearing. FIFO, auxiliary I2C, self-test response, motion
  detection, and cycle-mode timing are not simulated. Unmodeled addresses fail explicitly.
* P3T1755: all four registers, signed 12-bit samples, thresholds, shutdown, and one-shot.
  Resolution bits select conversion time, not output precision.
* TCN75A: all four registers, 9-12-bit quantization, thresholds, shutdown, and one-shot.
  Reset values follow register diagrams 5-4/5-5; the hexadecimal summary in Table 5-4 conflicts
  with those diagrams. Hardware observations recorded in its devicetree binding also question
  the datasheet's resolution behavior; this model follows the specified resolutions.
* TMP116/TMP117: register map, signed samples, mode changes, data-ready and alert flags,
  EEPROM unlock protection, and TMP117 offset/reset. EEPROM writes are immediately readable,
  but programming latency and nonvolatile retention across reset are not simulated.

GPIO interrupt delivery, thermostat fault queues, I3C, SPI, and I2C general-call reset are not
implemented. Storing a configuration bit does not imply that its associated feature is simulated.

Validation
**********

``tests/drivers/sensor/regmap`` runs independent register/protocol checks and shared driver
sample tests through Twister on ``native_sim`` and ``mps2/an385``. It includes both TMP11X variants,
negative values, one-shot instances, invalid channels, attributes, EEPROM, and bus errors.

Run ``west twister -p mps2/an385 -T tests/drivers/sensor/regmap --coverage`` to measure coverage.
For an initialization-only comparison, select ``-s drivers.sensor.regmap`` and add
``-x CONFIG_TEST_SENSOR_REGMAP_BASELINE=y`` with a separate output directory. This baseline is
initialization coverage for the same application, not repository-wide historical coverage.

Sources: `LPS22HB, DocID027083 Rev. 6`_, `MPU6050, RM-MPU-6000A-00 Rev. 4.0`_,
`P3T1755 Rev. 1.3`_, `TCN75A, DS21935D`_, `TMP116, SBOS740A`_, and `TMP117, SNOSD82D`_.

.. _LPS22HB, DocID027083 Rev. 6: https://www.st.com/resource/en/datasheet/lps22hb.pdf
.. _MPU6050, RM-MPU-6000A-00 Rev. 4.0: https://cdn.sparkfun.com/datasheets/Sensors/Accelerometers/RM-MPU-6000A.pdf
.. _P3T1755 Rev. 1.3: https://www.nxp.com/docs/en/data-sheet/P3T1755.pdf
.. _TCN75A, DS21935D: https://ww1.microchip.com/downloads/en/DeviceDoc/21935D.pdf
.. _TMP116, SBOS740A: https://www.ti.com/lit/ds/symlink/tmp116.pdf
.. _TMP117, SNOSD82D: https://www.ti.com/lit/ds/symlink/tmp117.pdf
