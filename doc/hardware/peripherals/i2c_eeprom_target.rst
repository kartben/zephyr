.. _i2c_eeprom_target_api:

I2C EEPROM Target
#################

Overview
********

The I2C EEPROM Target driver implements an I2C target (slave) device that
emulates a byte-addressable EEPROM. It is typically used in testing and
emulation scenarios where one Zephyr board acts as an I2C EEPROM that another
I2C controller can read from and write to.

The driver stores data in RAM and exposes an API to read and write that
backing store directly from application code, as well as to register a
callback that is invoked whenever the emulated EEPROM contents are modified
by an I2C transaction.

API Reference
**************

.. doxygengroup:: i2c_eeprom_target_api
