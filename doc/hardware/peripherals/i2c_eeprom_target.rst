.. _i2c_eeprom_target_api:

I2C EEPROM Target
#################

Overview
********

The I2C EEPROM Target driver emulates an EEPROM device on an I2C bus, allowing a Zephyr device
to act as an I2C target (peripheral) that appears to an I2C controller as a standard EEPROM.
This is useful for inter-processor communication where one processor acts as an I2C controller
and another provides data via a virtual EEPROM interface.

The I2C EEPROM Target API provides an interface for creating virtual EEPROM devices. Key features include:

**Virtual EEPROM Memory**
  Emulate EEPROM behavior using RAM buffers.
  Configurable memory size.

**Standard EEPROM Interface**
  Supports standard EEPROM read/write protocols.
  Compatible with I2C EEPROM controller drivers.
  Support for 8-bit and 16-bit address modes.

**Runtime Configuration**
  Program EEPROM contents from application code.
  Read individual bytes from virtual EEPROM.
  Change target I2C address at runtime (optional).

**I2C Target Mode**
  Operates in I2C target (peripheral) mode.
  Responds to I2C controller requests.

Devicetree Configuration
************************

I2C EEPROM Target devices are defined in the Devicetree as children of an I2C controller
with the ``zephyr,i2c-target-eeprom`` compatible property.

Example of an I2C EEPROM Target definition:

.. code-block:: devicetree

   &i2c0 {
       eeprom_target: eeprom@54 {
           compatible = "zephyr,i2c-target-eeprom";
           reg = <0x54>;
           size = <256>;
           address-width = <8>;
       };
   };

Properties:

- ``reg``: I2C target address (7-bit)
- ``size``: Virtual EEPROM size in bytes
- ``address-width``: Number of address bits (8 or 16)

Basic Operation
***************

I2C EEPROM Target operations involve programming the virtual EEPROM memory
and allowing an I2C controller to read from it.

.. code-block:: c
   :caption: Program and use a virtual I2C EEPROM

   #include <zephyr/drivers/i2c/target/eeprom.h>

   #define EEPROM_TARGET_NODE DT_NODELABEL(eeprom_target)

   void setup_eeprom_target(void)
   {
       const struct device *eeprom_dev = DEVICE_DT_GET(EEPROM_TARGET_NODE);
       uint8_t eeprom_data[256];
       uint8_t read_byte;
       int ret;

       if (!device_is_ready(eeprom_dev)) {
           printk("EEPROM target device not ready\n");
           return;
       }

       /* Initialize EEPROM contents */
       for (int i = 0; i < sizeof(eeprom_data); i++) {
           eeprom_data[i] = i;
       }

       /* Program the virtual EEPROM */
       ret = eeprom_target_program(eeprom_dev, eeprom_data,
                                   sizeof(eeprom_data));
       if (ret < 0) {
           printk("Failed to program EEPROM: %d\n", ret);
           return;
       }

       /* Read back a byte from the virtual EEPROM */
       ret = eeprom_target_read(eeprom_dev, &read_byte, 0x10);
       if (ret < 0) {
           printk("Failed to read from EEPROM: %d\n", ret);
           return;
       }

       printk("EEPROM byte at offset 0x10: 0x%02x\n", read_byte);

       /* The I2C controller can now read from this device at address 0x54 */
   }

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_I2C_TARGET`
* :kconfig:option:`CONFIG_I2C_EEPROM_TARGET`
* :kconfig:option:`CONFIG_I2C_EEPROM_TARGET_RUNTIME_ADDR`

API Reference
**************

.. doxygengroup:: i2c_eeprom_target_api
