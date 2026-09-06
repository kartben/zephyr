.. _sensor-emulators:

Emulators
#########

A sensor emulator stands in for the sensor on an emulated bus (:kconfig:option:`CONFIG_EMUL`) so
the driver can be exercised on :ref:`native_sim <native_sim>`. Emulators that implement the
:ref:`sensor emulator backend API <sensor-api-reference>` are picked up by the generic sensor test
in :zephyr_file:`tests/drivers/build_all/sensor`, which sets a value on every channel the emulator
reports and checks that the driver reads it back.

Register map based emulators
****************************

Most I2C sensors are a register map. :c:macro:`EMUL_SENSOR_REGMAP_DEFINE` builds a complete
emulator from that map, transcribed from the datasheet. Do not read the driver while writing it:
the table describes the device, and a mismatch with the driver is a finding, not something to
paper over.

From datasheet to table
=======================

The walkthrough uses the NXP P3T1755 temperature sensor. Its complete emulator is
:zephyr_file:`drivers/sensor/nxp/p3t1755/p3t1755_emul.c`.

1. **Find the I2C protocol section.** Note the width of a register (one byte, or two as here)
   and which byte comes first on the bus. "MSByte first" is ``.big_endian = true``; "low byte at
   the lower address" is the default. If the register address byte carries bits that are not
   part of the address (an auto-increment bit, unused upper bits), put them in ``.addr_ignore``.

2. **Find the register map table.** Copy every row: address, name, whether it is read only,
   the reset value, and the width when it differs from the device default. Reserved addresses
   can be skipped. For the P3T1755 the table has four rows:

   .. code-block:: c

      static const struct emul_sensor_reg p3t1755_regs[] = {
              {0x00, "Temp", EMUL_SENSOR_REG_RO},
              {0x01, "Conf", .bytes = 1, .reset = 0x28},
              {0x02, "TLOW", .reset = 0x4B00},
              {0x03, "THIGH", .reset = 0x5000},
      };

   While reading the register descriptions, note bits with special behavior and add them to
   the row:

   * "self-clears", "returns to 0 when the conversion completes", "the bit is automatically
     cleared": ``.self_clear = BIT(n)``.
   * "cleared when the register is read", "reading this register clears the interrupt":
     ``.clear_on_read = BIT(n)``.
   * "reserved, write 0", or a read-only status bit inside a writable register:
     ``.write_mask`` listing the writable bits.

3. **Find the output data format.** For each measurement the datasheet gives the register
   holding the sample, its width, whether it is two's complement, where it sits in the word
   (right justified, or left justified with unused low bits), the value of one LSB and the
   value of a zero code. The P3T1755 stores a 12-bit two's complement value in bits 15:4 at
   0.0625 degC per LSB:

   .. code-block:: c

      static const struct emul_sensor_channel p3t1755_channels[] = {
              {SENSOR_CHAN_AMBIENT_TEMP, .reg = 0x00, .is_signed = true, .bits = 12, .pos = 4,
               .lsb = 0.0625, .min = -40.0, .max = 125.0},
      };

   ``lsb`` and ``offset`` are in the unit of the Zephyr channel: degC, m/s^2 (1 g is
   9.80665), rad/s, gauss, kPa, percent, lux, degrees. A formula such as "T = code / 340 +
   36.53" is ``.lsb = 1.0 / 340, .offset = 36.53``. ``min`` and ``max`` come from the
   specifications table; leave both at zero to use the whole field. A sample spread over
   several registers starts at the first one and takes as many consecutive registers as it
   needs, in the byte order of the device.

4. **Check whether the format depends on a configuration register.** A full-scale range,
   gain, integration time or resolution selection changes the LSB, sometimes the field
   layout and the range. Name the selecting bits in ``select`` and give one variant per value
   of that field, as the MPU6050 does for its accelerometer range:

   .. code-block:: c

      .select = {0x1C, GENMASK(4, 3)},
      .variants = {{.lsb = G / 16384}, {.lsb = G / 8192}, {.lsb = G / 4096}, {.lsb = G / 2048}},

   A variant member left at zero inherits the channel value, so only what changes is listed.
   The TCN75A resolution bits change the field width and position as well as the LSB; the
   MAX31875 data format bit adds a range bit. Both are in
   :zephyr_file:`drivers/sensor/microchip/tcn75a/tcn75a_emul.c` and
   :zephyr_file:`drivers/sensor/maxim/max31875/max31875_emul.c`.

5. **Find the data ready flag.** If a status register has a bit meaning "new data available",
   name it in ``ready`` so it is set whenever a sample is written:

   .. code-block:: c

      .ready = {0x27, BIT(0)},

6. **Instantiate.** ``DT_DRV_COMPAT`` is the devicetree compatible of the driver with commas
   and dashes replaced by underscores. The trailing arguments are the device wide settings
   from step 1:

   .. code-block:: c

      #define DT_DRV_COMPAT nxp_p3t1755

      EMUL_SENSOR_REGMAP_DEFINE(p3t1755_regs, p3t1755_channels, .reg_bytes = 2, .big_endian = true);

   Name the file ``<driver>_emul.c`` next to the driver and add it to its ``CMakeLists.txt``:

   .. code-block:: cmake

      zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP p3t1755_emul.c)

What the emulated sensor reports
================================

Nothing measured. The emulator has no physics and no clock: it answers every bus access with
the current content of its registers. After reset a data register holds the reset value from
the table, so the P3T1755 above reports 0.0 degC on every read, and a sensor whose zero code
is not zero reports that constant (36.53 degC for the MPU6050). Configuration writes from the
driver are stored and read back, and a status bit named in ``ready`` is set when a value is
injected, but no conversion ever takes place on its own.

A value is injected through the sensor emulator backend API, in the channel's SI unit as a
Q31 fixed-point number with a shift:

.. code-block:: c

   const struct emul *emul = EMUL_DT_GET(DT_NODELABEL(my_sensor));
   struct sensor_chan_spec ch = {.chan_type = SENSOR_CHAN_AMBIENT_TEMP};
   int8_t shift = 8;
   q31_t value = (q31_t)(23.5 * (1LL << 31) / (1 << shift));

   emul_sensor_backend_set_channel(emul, ch, &value, shift);

The emulator converts 23.5 degC to the register code the datasheet specifies and the driver
then reads 23.5 degC. This is what the generic test does for five values across the range of
every channel. A test that needs a specific register content can write the code directly with
:c:func:`emul_sensor_regmap_set_reg`.

Running it
==========

Every sensor in :zephyr_file:`tests/drivers/build_all/sensor/i2c.dtsi` gets a test case in the
generic test, which skips sensors without an emulator and fails those whose driver does not
read back the values the emulator was given:

.. code-block:: console

   west twister -p native_sim -T tests/drivers/build_all/sensor -s drivers.sensor.generic_test -i

If the sensor is not listed there yet, add a node for it. Then read the ``handler.log`` of the
run for the test named after the node:

* **SKIP**: no emulator was registered. The file is not built or ``DT_DRV_COMPAT`` is wrong.
* ``read of unknown register 0x..`` or ``write of unknown register 0x..``: the driver touches
  a register that is not in the table. Add the row from the datasheet.
* ``Expected ... got ...`` on a channel: the emulator wrote a code that the driver decoded to
  a different value. Compare the trace of register accesses, printed by name at debug level
  (:kconfig:option:`CONFIG_SENSOR_LOG_LEVEL_DBG`), with the datasheet. Either the table
  misreads the datasheet, or the driver does: check the sign handling, the LSB and the
  offset. Fix whichever is wrong; do not adjust the table to match a wrong driver.
* ``Could not decode``: the driver does not expose the channel, or needs a Kconfig option
  or devicetree property to expose it.

What the table cannot say
=========================

Command based devices that append a CRC to their response, calibration compensated outputs
(BME280), bank switched register maps and SPI transfers are outside this framework. Behavior
beyond what the table expresses (an interrupt being asserted, a FIFO, a conversion taking
time) is not simulated: the emulator answers the bus with the state of its registers, nothing
more.

API Reference
*************

.. doxygengroup:: emul_sensor_regmap
