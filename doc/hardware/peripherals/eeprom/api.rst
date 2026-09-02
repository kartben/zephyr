.. _eeprom_api:

EEPROM API
##########

Overview
********

An Electrically Erasable Programmable Read-Only Memory (EEPROM) is a non-volatile memory that can be
erased and rewritten one byte at a time: no block erase has to precede a write, any byte can be
overwritten in place, and the content survives resets and power cycles. EEPROMs exist as external
I2C or SPI chips (such as the Atmel AT24 and AT25 families and their compatibles), as a data memory
built into some SoCs (such as the STM32 L0 and L1 families), and as devices that Zephyr emulates on
top of a flash partition or simulates in RAM. Zephyr can also act as an EEPROM towards another I2C
controller; that is the separate :ref:`i2c_eeprom_target_api`.

The EEPROM API presents every such device as a flat, byte addressable array of fixed size accessed
through three calls: :c:func:`eeprom_read`, :c:func:`eeprom_write` and :c:func:`eeprom_get_size`.
Bus transactions, chip page boundaries and write cycle timing are handled by the driver, so the
application code is the same whatever the underlying device. Key concepts include:

**EEPROM device**
  A :c:struct:`device` whose driver implements the :c:struct:`eeprom_driver_api` operations
  ``read``, ``write`` and ``size``; all three are mandatory. Applications obtain the device from
  devicetree, usually through the ``eeprom-0`` alias used by boards, shields, the sample and the
  tests.

**Offset and length**
  Data is addressed by a byte offset (``off_t``) from the start of the EEPROM and a length in bytes.
  The API imposes no alignment or block size requirements: any range that lies within the device can
  be read or written with a single call.

**Size**
  :c:func:`eeprom_get_size` returns the capacity in bytes. Most drivers take it from the ``size``
  devicetree property; the STM32 driver derives it from the ``reg`` property of the on-chip node.

Devicetree Configuration
************************

:kconfig:option:`CONFIG_EEPROM` enables the driver class; the driver for a device is selected
automatically when a node with its compatible is enabled in devicetree, for example
:kconfig:option:`CONFIG_EEPROM_AT24` for :dtcompatible:`atmel,at24` nodes.
:kconfig:option:`CONFIG_EEPROM_INIT_PRIORITY` sets the initialization priority of drivers that do
not define their own. The AT24 and AT25 driver uses
:kconfig:option:`CONFIG_EEPROM_AT2X_INIT_PRIORITY` so that it initializes after its bus, and the
emulated EEPROM driver fails to initialize if the flash device holding its partition is not ready
yet, which may require raising :kconfig:option:`CONFIG_EEPROM_INIT_PRIORITY` as the API test does
for ``qemu_x86``.

EEPROM bindings include :zephyr_file:`dts/bindings/mtd/eeprom-base.yaml`, which defines ``size``
(the total size in bytes, required by the AT24, AT25, emulated, simulated and fake EEPROM bindings)
and ``read-only`` (a boolean that makes the driver reject every write). Chips compatible with the
AT24 (I2C) and AT25 (SPI) families also require ``pagesize`` (the write page size in bytes),
``address-width`` (8 or 16 bits for AT24, 8, 16 or 24 bits for AT25) and ``timeout`` (the write
cycle timeout in milliseconds), and accept an optional ``wp-gpios`` write protect pin that the
driver releases only while a write is in progress. The following example, adapted from the
``x_nucleo_eeprma2`` shield, describes an ST M24C02 on an I2C bus and exposes it through the
``eeprom-0`` alias:

.. code-block:: devicetree

   / {
       aliases {
           eeprom-0 = &eeprom0;
       };
   };

   &i2c0 {
       status = "okay";

       eeprom0: eeprom@54 {
           compatible = "st,m24c02", "st,m24xxx", "atmel,at24";
           reg = <0x54>;
           size = <256>;
           pagesize = <16>;
           address-width = <8>;
           timeout = <5>;
       };
   };

Emulated, simulated and fake EEPROMs
====================================

When no EEPROM hardware is available, :kconfig:option:`CONFIG_EEPROM_EMULATOR` provides one on top
of a flash partition through the :dtcompatible:`zephyr,emu-eeprom` binding, which requires ``size``,
``pagesize`` and ``partition`` (a phandle to the flash partition). The partition is divided into
EEPROM pages of ``pagesize`` bytes, each holding a full image of the EEPROM data followed by a log
of (address, data) change records. Writes append records to the log instead of rewriting the image,
which maximizes the number of writes the flash can absorb. When the log is full, the driver copies
the data with all changes applied into the next page (wrapping around to the start of the
partition) and invalidates the old one; at initialization it locates the valid page and completes a
copy that a power loss interrupted. The geometry is checked at build time: the partition size must
be a multiple of ``pagesize``, a page must leave room for changes (``4 * size`` must not exceed
``3 * pagesize``), and the partition must hold at least two pages. A read-only device or one using
``partition-erase`` only needs ``size`` not to exceed ``pagesize`` and a single page.

Two optional booleans tune the driver. ``rambuf`` keeps a copy of the whole EEPROM in RAM so that
reads are served from memory instead of replaying the change log. ``partition-erase`` postpones
erasing until the whole partition has been used; it implies a RAM buffer, and the data is lost if
power fails during the partition erase. The device is read-only when either its own node or the
partition node has ``read-only``. See :zephyr_file:`boards/qemu/x86/qemu_x86.dts` for a 4 KiB EEPROM
emulated on 8 KiB pages inside a 64 KiB partition of the simulated flash.

Two further drivers serve tests. :kconfig:option:`CONFIG_EEPROM_SIMULATOR` implements
:dtcompatible:`zephyr,sim-eeprom`, which keeps the content in RAM, or on :zephyr:board:`native_sim`
in a host file (``eeprom.bin`` by default) that persists across runs; it backs the ``eeprom-0``
alias of that board (see :ref:`emul_eeprom_simu_brief`).
:kconfig:option:`CONFIG_EEPROM_FAKE` implements :dtcompatible:`zephyr,fake-eeprom`, an FFF based
fake declared in :zephyr_file:`include/zephyr/drivers/eeprom/eeprom_fake.h` whose operations a test
can stub, as the shell test in :zephyr_file:`tests/drivers/eeprom/shell` does.

Typical application flow
************************

#. Describe the EEPROM in the board devicetree, a shield or an application overlay, and enable
   :kconfig:option:`CONFIG_EEPROM`.
#. Obtain the device with :c:macro:`DEVICE_DT_GET`, typically from the ``eeprom-0`` alias, and
   check it with :c:func:`device_is_ready`.
#. Query the capacity with :c:func:`eeprom_get_size` and check that the application data layout
   fits within it.
#. Read stored data with :c:func:`eeprom_read` and validate it, for example with a magic value,
   since an EEPROM that has never been written holds no meaningful content.
#. Update data in place with :c:func:`eeprom_write`.
#. Check the return value of every call: failures are reported as negative errno codes.

Basic Operation
***************

The following example, derived from the :zephyr:code-sample:`eeprom` sample, keeps a boot counter
at the start of the EEPROM referenced by the ``eeprom-0`` alias. Reads and writes take a plain
buffer, so a structure is stored and retrieved with a single call each:

.. code-block:: c

   #define BOOT_RECORD_MAGIC 0xEE9703

   struct boot_record {
       uint32_t magic;
       uint32_t boot_count;
   };

   static const struct device *const eeprom = DEVICE_DT_GET(DT_ALIAS(eeprom_0));

   /* Increment the boot counter stored at the start of the EEPROM */
   int boot_count_update(void)
   {
       struct boot_record record;
       int rc;

       if (!device_is_ready(eeprom) || eeprom_get_size(eeprom) < sizeof(record)) {
           return -ENODEV;
       }

       rc = eeprom_read(eeprom, 0, &record, sizeof(record));
       if (rc < 0) {
           return rc;
       }

       if (record.magic != BOOT_RECORD_MAGIC) {
           /* Never written before: start counting from zero */
           record.magic = BOOT_RECORD_MAGIC;
           record.boot_count = 0;
       }

       record.boot_count++;
       printk("Device booted %u times\n", record.boot_count);

       return eeprom_write(eeprom, 0, &record, sizeof(record));
   }

With :kconfig:option:`CONFIG_NVMEM_EEPROM`, an EEPROM node can also carry an ``nvmem-layout`` child
describing named cells that consumers access through the :ref:`NVMEM API <nvmem>`, which forwards
cell reads and writes to :c:func:`eeprom_read` and :c:func:`eeprom_write`.

Blocking behavior, errors and thread safety
===========================================

All three calls are synchronous; the API has no asynchronous or callback based variant.
:c:func:`eeprom_read` returns once the data is in the buffer and :c:func:`eeprom_write` once the
data has been handed to the device. The AT24 and AT25 driver splits a write into chunks that never
cross a ``pagesize`` boundary and, before each transfer, waits for the previous internal write
cycle to finish by polling the chip until it responds or ``timeout`` expires, so writing a large
buffer spans several write cycles. Drivers follow common conventions, which the API test in
:zephyr_file:`tests/drivers/eeprom/api` exercises:

* A zero length read or write succeeds without touching the device.
* A range that extends past the end of the EEPROM (``offset + len`` larger than the size) is
  rejected with ``-EINVAL`` and nothing is transferred.
* The AT24 and AT25, emulated and simulated drivers reject writes to a device marked ``read-only``
  with ``-EACCES``.
* Bus and flash errors are propagated as the negative errno code of the underlying transfer.

Drivers serialize accesses to a device with a lock, and bus attached drivers sleep while waiting
for a write cycle, so the API must be called from thread context only, never from an interrupt
handler. Calls from several threads to the same device are safe but execute one at a time. The API
defines no power management operations of its own; drivers that need them, such as the Microchip
XEC driver, implement :ref:`device power management <pm_api>` individually.

The three functions are system calls. With :kconfig:option:`CONFIG_USERSPACE`, a user mode thread
can use them on a device it has been granted access to with :c:func:`k_object_access_grant`; the
kernel checks that the data buffer is accessible to the caller before the driver runs. The API test
runs its test cases as user threads to cover this path.

Shell commands
**************

When :kconfig:option:`CONFIG_EEPROM_SHELL` is enabled (it depends on
:kconfig:option:`CONFIG_SHELL`), an ``eeprom`` command with the following subcommands is available.
Each subcommand takes the name of the EEPROM device as its first argument, with tab completion;
offsets, lengths and data bytes accept decimal or ``0x`` prefixed hexadecimal values.

``eeprom read <device> <offset> <length>``
  Read ``length`` bytes starting at ``offset`` and print them as a hex dump.

``eeprom write <device> <offset> [byte0] <byte1> .. <byteN>``
  Write the given bytes starting at ``offset``, then read them back and verify them. At most
  :kconfig:option:`CONFIG_EEPROM_SHELL_BUFFER_SIZE` bytes can be written with one command.

``eeprom size <device>``
  Print the size of the EEPROM in bytes.

``eeprom fill <device> <offset> <length> <pattern>``
  Write ``length`` copies of the byte ``pattern`` starting at ``offset`` and verify the result.

See :ref:`eeprom_shell` for build instructions and example sessions.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_EEPROM`
* :kconfig:option:`CONFIG_EEPROM_INIT_PRIORITY`
* :kconfig:option:`CONFIG_EEPROM_SHELL`
* :kconfig:option:`CONFIG_EEPROM_SHELL_BUFFER_SIZE`
* :kconfig:option:`CONFIG_EEPROM_EMULATOR`
* :kconfig:option:`CONFIG_EEPROM_SIMULATOR`
* :kconfig:option:`CONFIG_NVMEM_EEPROM`

API Reference
*************

.. doxygengroup:: eeprom_interface
