.. _i2c_eeprom_target_api:

I2C EEPROM Target
#################

Overview
********

An I2C EEPROM is a small serial memory that a bus controller accesses in two steps: it first writes
the memory address it wants to work on, then transfers data bytes while the memory advances an
internal address pointer after each byte. The I2C EEPROM target driver makes a Zephyr device behave
like such a memory. It owns a RAM buffer and exposes it on an I2C bus through the target mode of the
bus controller (see :ref:`i2c-target-api`), so that an external controller, for example a host
processor, can read and write the buffer exactly as it would read and write a real EEPROM.

At the same time, the application reads and writes the same buffer from software, which makes the
driver a simple shared-memory mailbox between a Zephyr application and an external controller. It is
also the device used to exercise the target mode of I2C controller drivers.

This driver is not related to the :ref:`eeprom_api`, which drives real EEPROM chips from the
controller side of the bus.

Key concepts:

**Virtual EEPROM buffer**
  The driver allocates a buffer of ``size`` bytes, as declared in devicetree, for each instance.
  The application accesses it with :c:func:`eeprom_target_read_data` and
  :c:func:`eeprom_target_write_data`, and queries its size with :c:func:`eeprom_target_get_size`.

**Target registration**
  The EEPROM device is a child node of an I2C controller node and implements the
  :c:struct:`i2c_target_driver_api`. The application attaches it to the bus with
  :c:func:`i2c_target_driver_register` and detaches it with
  :c:func:`i2c_target_driver_unregister`. Internally, the driver calls
  :c:func:`i2c_target_register` on its parent controller with a :c:struct:`i2c_target_config`
  holding the address from the ``reg`` property and its :c:struct:`i2c_target_callbacks`.

**Memory addressing**
  The ``address-width`` property selects whether a controller sends one or two address bytes at the
  start of a write. The internal address pointer wraps around at the end of the buffer.

**Change notification**
  A handler of type :c:type:`eeprom_target_changed_handler_t`, installed with
  :c:func:`eeprom_target_set_changed_callback`, runs whenever a bus transaction has modified the
  buffer contents.

**Runtime address change**
  With :kconfig:option:`CONFIG_I2C_EEPROM_TARGET_RUNTIME_ADDR`, :c:func:`eeprom_target_set_addr`
  moves a registered target to a different I2C address.

Bus protocol
************

The driver implements the byte-level target callbacks of the :ref:`i2c-target-api` and turns them
into operations on the buffer:

* A **write** transaction starts with the address bytes: one byte when ``address-width`` is 8 (the
  default), two bytes with the most significant one first when it is 16. They load the internal
  address pointer. Every following data byte is stored at the pointer position, and the pointer is
  incremented. A write that carries only address bytes simply positions the pointer.
* A **read** transaction returns the byte at the pointer position, then advances the pointer before
  each further byte, so the pointer stays on the last byte returned. The pointer is kept between
  transactions, so a controller usually writes the address bytes and then reads after a repeated
  start, which is what :c:func:`i2c_write_read` does on the controller side.
* In both directions the pointer wraps around modulo the buffer size, so a transfer that reaches the
  end of the buffer continues from offset 0.
* When the controller issues a **stop** condition, the driver invokes the change handler if the
  transaction modified the buffer.

The driver acknowledges every byte written to it: the virtual EEPROM cannot be made read-only.

The following controller-side code, adapted from the driver test, sets the pointer to ``0x10`` and
then reads eight bytes from a virtual EEPROM registered at address ``0x54``:

.. code-block:: c

   uint8_t offset = 0x10;
   uint8_t buf[8];
   int ret;

   ret = i2c_write_read(i2c_ctrl, 0x54, &offset, sizeof(offset), buf, sizeof(buf));

When :kconfig:option:`CONFIG_I2C_TARGET_BUFFER_MODE` is enabled, the driver also provides the
buffer-mode callbacks for controllers that hand over a complete transfer at once instead of one byte
at a time. The address bytes are interpreted in the same way.

Devicetree Configuration
************************

A virtual EEPROM is described by a child node of the I2C controller whose target mode it uses, with
the :dtcompatible:`zephyr,i2c-target-eeprom` compatible. The binding defines these properties:

``reg``
  The 7-bit I2C address at which the target answers.

``size``
  The size of the virtual EEPROM in bytes.

``address-width``
  The number of address bits sent by the controller at the start of a transaction, ``8`` (the
  default) or ``16``. The driver checks at build time that ``size`` does not exceed
  ``2 ^ address-width``.

The example below, adapted from the board overlays of the driver test, exposes a 256-byte virtual
EEPROM at address ``0x54`` on ``i2c1``:

.. code-block:: devicetree

   &i2c1 {
       eeprom0: eeprom@54 {
           compatible = "zephyr,i2c-target-eeprom";
           reg = <0x54>;
           size = <256>;
       };
   };

Buffers larger than 256 bytes require 16-bit addressing:

.. code-block:: devicetree

   &i2c1 {
       eeprom0: eeprom@54 {
           compatible = "zephyr,i2c-target-eeprom";
           reg = <0x54>;
           address-width = <16>;
           size = <1024>;
       };
   };

:kconfig:option:`CONFIG_I2C_EEPROM_TARGET` is enabled by default when such a node is enabled, but it
lives under :kconfig:option:`CONFIG_I2C_TARGET`, which the application must turn on explicitly
together with :kconfig:option:`CONFIG_I2C`. The parent controller driver must support the target
role. On some SoCs that role is provided by a separate peripheral with its own compatible; the
overlays in :zephyr_file:`tests/drivers/i2c/i2c_target_api/boards` show working configurations for
many boards.

Basic Operation
***************

Typical application flow
========================

#. Enable :kconfig:option:`CONFIG_I2C`, :kconfig:option:`CONFIG_I2C_TARGET` and
   :kconfig:option:`CONFIG_I2C_EEPROM_TARGET`, and describe the virtual EEPROM in devicetree.
#. Get the device with :c:macro:`DEVICE_DT_GET` and check it with :c:func:`device_is_ready`. The
   device is initialized in the ``POST_KERNEL`` level at
   :kconfig:option:`CONFIG_I2C_TARGET_INIT_PRIORITY` and fails its initialization when the parent
   controller is not ready.
#. Optionally preload the buffer with :c:func:`eeprom_target_write_data` and install a change
   handler with :c:func:`eeprom_target_set_changed_callback`. Both can be done before the target is
   visible on the bus.
#. Call :c:func:`i2c_target_driver_register` to attach the target to its bus. From this point on, an
   external controller can access the buffer.
#. Read what the controller wrote with :c:func:`eeprom_target_read_data`, typically from the change
   handler or from a thread it notifies, and update the data the controller will read next with
   :c:func:`eeprom_target_write_data`.
#. Call :c:func:`i2c_target_driver_unregister` to detach the target when it is no longer needed.

Registering the target
======================

The following example, adapted from the :zephyr:code-sample:`i2c-eeprom-target` sample, preloads
the buffer, installs a change handler and registers the target defined by the ``eeprom0`` node:

.. code-block:: c

   #include <zephyr/device.h>
   #include <zephyr/drivers/i2c.h>
   #include <zephyr/drivers/i2c/target/eeprom.h>
   #include <zephyr/kernel.h>
   #include <zephyr/sys/printk.h>

   static const struct device *const eeprom = DEVICE_DT_GET(DT_NODELABEL(eeprom0));

   static void on_changed(const struct device *dev, void *user_data)
   {
       uint8_t first;

       /* A controller has just written to the virtual EEPROM */
       eeprom_target_read_data(dev, 0, &first, sizeof(first));
       printk("Byte 0 is now 0x%02x\n", first);
   }

   int main(void)
   {
       static const uint8_t initial[] = {0x01, 0x02, 0x03, 0x04};
       int ret;

       if (!device_is_ready(eeprom)) {
           return -ENODEV;
       }

       /* Preload the buffer before it becomes visible on the bus */
       ret = eeprom_target_write_data(eeprom, 0, initial, sizeof(initial));
       if (ret < 0) {
           return ret;
       }

       eeprom_target_set_changed_callback(eeprom, on_changed, NULL);

       ret = i2c_target_driver_register(eeprom);
       if (ret < 0) {
           return ret;
       }

       /* The external controller can now read and write the buffer */

       return 0;
   }

Accessing the buffer from the application
=========================================

:c:func:`eeprom_target_read_data` and :c:func:`eeprom_target_write_data` copy ``len`` bytes between
the buffer, starting at ``offset``, and application memory. Both return ``-EINVAL`` and log a
warning when ``offset + len`` exceeds the size reported by :c:func:`eeprom_target_get_size`. They
work whether or not the target is registered. Two inline helpers remain for older code:
:c:func:`eeprom_target_read` reads a single byte, and :c:func:`eeprom_target_program`, which is
deprecated, writes a block at offset 0.

The driver does not serialize application accesses against bus transactions. A read from the
application while a controller is in the middle of a write can observe partially updated data, and
an application write can interleave with a controller read. The application and the controller have
to agree on when a region of the buffer is complete, for example by acting on the change
notification described below or by reserving a flag byte that the writer sets last.

Change notification
===================

The change handler is the only asynchronous notification the driver offers. It is invoked from the
stop callback of the target API after a transaction that wrote at least one data byte into the
buffer; reads and address-only writes do not trigger it. The handler receives the EEPROM device and
the ``user_data`` pointer given to :c:func:`eeprom_target_set_changed_callback`, but not the offset
or length of the modified region, so it usually reads back the area it is interested in with
:c:func:`eeprom_target_read_data`.

The target callbacks, and hence the change handler, are executed by the parent controller driver as
bus events are processed, typically from its interrupt handler. The handler must be short and must
not block; longer processing belongs in a work item or in a thread signaled from the handler.
Installing the handler before calling :c:func:`i2c_target_driver_register` guarantees that no write
is missed.

Changing the target address at runtime
======================================

When :kconfig:option:`CONFIG_I2C_EEPROM_TARGET_RUNTIME_ADDR` is enabled,
:c:func:`eeprom_target_set_addr` changes the address of a registered target. The driver unregisters
the target from its controller, stores the new address in its :c:struct:`i2c_target_config` and
registers it again. It returns the error of whichever step fails: ``-ENOSYS`` when the controller
does not implement the target API, ``-EINVAL`` for invalid parameters or ``-EIO`` for a bus error.
Without this option the function is not compiled in.

Constraints
***********

* The parent controller must implement the target side of the :ref:`i2c-target-api`, which is still
  experimental and available in a limited number of controller drivers. When it is missing,
  :c:func:`i2c_target_driver_register` fails with ``-ENOSYS``.
* The target is registered with a 7-bit address; the driver never sets
  :c:macro:`I2C_TARGET_FLAGS_ADDR_10_BITS`.
* Whether a controller can serve as a target while also initiating transactions depends on the
  hardware. The driver test only exercises both roles on the same controller when its
  ``CONFIG_APP_DUAL_ROLE_I2C`` option is set.
* :c:func:`i2c_target_driver_register` and :c:func:`i2c_target_driver_unregister` are system calls
  and can be used from user mode threads. The ``eeprom_target_*`` functions are not system calls and
  therefore cannot be called from user mode.
* The driver implements no device power management hooks.

Driver implementation notes
***************************

The EEPROM driver itself is a thin layer. All of the bus handling lives in the register set target
library (:zephyr_file:`drivers/i2c/target/regset_target_lib.c`, selected through
:kconfig:option:`CONFIG_I2C_TARGET_REGSET_LIB`), which provides the :c:struct:`i2c_target_callbacks`
implementation, the :c:struct:`i2c_target_driver_api` instance and the devicetree helpers that
populate the per-instance configuration from the ``reg``, ``size`` and ``address-width``
properties. The same library backs :dtcompatible:`zephyr,i2c-target-tmp103`, which emulates a TMP103
temperature sensor, and it is the starting point for emulating other register-based I2C devices: a
driver embeds the library configuration and data structures as the first members of its own,
forwards its initialization to the library and points its device API at the library implementation.
The common devicetree properties are declared in
:zephyr_file:`dts/bindings/i2c/regset-target-base.yaml`.

Samples and tests
*****************

The :zephyr:code-sample:`i2c-eeprom-target` sample registers a virtual EEPROM and prints its
contents whenever a controller writes to it.

The :zephyr_file:`tests/drivers/i2c/i2c_target_api` test uses two virtual EEPROMs, each attached to
a different controller of the same SoC, with the SDA and SCL lines of the two controllers shorted
together. It preloads the buffers through :c:func:`eeprom_target_write_data`, registers them, and
then verifies full, partial and write-then-read accesses issued through one controller against the
data known to be present on the other. It is the reference for validating the target mode of a new
controller driver; the overlays in its ``boards`` directory document the required wiring.

Shell commands
**************

When :kconfig:option:`CONFIG_I2C_SHELL` is enabled together with
:kconfig:option:`CONFIG_I2C_TARGET`, the ``i2c`` shell command gains a ``target`` group that
attaches and detaches target devices without application code. The argument is the name of the
target device, which is the node name from devicetree, for example ``eeprom@54``.

``i2c target register <device>``
  Register the target device on its bus, as :c:func:`i2c_target_driver_register` does.

``i2c target unregister <device>``
  Unregister the target device from its bus, as :c:func:`i2c_target_driver_unregister` does.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_I2C`
* :kconfig:option:`CONFIG_I2C_TARGET`
* :kconfig:option:`CONFIG_I2C_EEPROM_TARGET`
* :kconfig:option:`CONFIG_I2C_EEPROM_TARGET_RUNTIME_ADDR`
* :kconfig:option:`CONFIG_I2C_TARGET_BUFFER_MODE`
* :kconfig:option:`CONFIG_I2C_TARGET_INIT_PRIORITY`
* :kconfig:option:`CONFIG_I2C_SHELL`

API Reference
*************

.. doxygengroup:: i2c_eeprom_target_api
