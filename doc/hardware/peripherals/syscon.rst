.. _syscon_api:

System Controller (syscon)
##########################

Overview
********

A system controller (syscon) is a block of memory-mapped registers that does not belong to any
single peripheral. It typically holds miscellaneous SoC-level controls such as clock gates, reset
lines, pad configuration, write protection keys or chip identification fields. Because these
registers are not cohesive enough to be modeled as one specific device class, Zephyr exposes the
whole region as a generic ``syscon`` device.

The syscon API gives other drivers and platform code a uniform way to obtain a reference to such a
region and to read, write or update individual registers in it by byte offset, without knowing how
the region is mapped. The generic driver in :zephyr_file:`drivers/syscon/syscon.c` implements the
API for any devicetree node with the :dtcompatible:`syscon` compatible. Key concepts include:

**Register region**
  A syscon device covers one contiguous address range described by the ``reg`` property of its
  devicetree node. :c:func:`syscon_get_base` returns the address at which the driver accesses the
  region and :c:func:`syscon_get_size` returns its length in bytes.

**Register offset**
  Registers are addressed by their byte offset from the start of the region.
  :c:func:`syscon_read_reg` and :c:func:`syscon_write_reg` transfer one register at a time as a
  32-bit value.

**Register width**
  The optional ``reg-io-width`` devicetree property declares how wide each register is (1, 2 or 4
  bytes, 4 by default). The generic driver uses it to select the bus access size and to reject
  misaligned offsets.

**Atomic bit update**
  :c:func:`syscon_update_bits` performs a read-modify-write of one register under the driver's
  lock, replacing only the bits selected by a mask. Consumers that share a register use it instead
  of an unlocked read followed by a write.

**Driver API**
  Drivers implement :c:struct:`syscon_driver_api`. Every operation in the structure is optional;
  calling an operation the driver does not provide returns ``-ENOSYS``.

Devicetree Configuration
************************

A generic syscon node needs the :dtcompatible:`syscon` compatible and a ``reg`` property. The
``reg-io-width`` property is optional and selects the register width in bytes; it defaults to 4.
Both nodes below are taken from :zephyr_file:`dts/arm/nuvoton/npcm/npcm.dtsi`, where two adjacent
register groups have different widths:

.. code-block:: devicetree

   mdc: mdc@4000c000 {
       compatible = "syscon";
       reg = <0x4000c000 0xa>;
       reg-io-width = <1>;
   };

   mdc_header: mdc@4000c00a {
       compatible = "syscon";
       reg = <0x4000c00a 0x4>;
       reg-io-width = <2>;
   };

Nodes often list a hardware-specific compatible first and ``syscon`` as a fallback. The specific
compatible documents what the block is, while the ``syscon`` fallback makes the generic driver bind
to it. Consumers then reference the node through a phandle property defined in their own binding,
as the NEORV32 peripherals do with their ``syscon`` property
(:zephyr_file:`dts/riscv/neorv32.dtsi`):

.. code-block:: devicetree

   gpio: gpio@fffc0000 {
       compatible = "neorv32,gpio";
       reg = <0xfffc0000 0x10000>;
       syscon = <&sysinfo>;
       gpio-controller;
       #gpio-cells = <2>;
   };

   sysinfo: syscon@fffe0000 {
       compatible = "neorv-sysinfo", "syscon";
       reg = <0xfffe0000 0x10000>;
   };

A consumer binding can also carry a register offset and a value or bit number along with the
phandle by using a phandle array with specifier cells. :dtcompatible:`ti,control-module` extends
the generic binding this way with its ``#clksel-cells`` and ``#epwm-tbclk-cells`` properties, and
adds ``ti,unlock-offsets`` to list the write-protected partitions that SoC code unlocks at boot.
The :dtcompatible:`sifli,sf32lb-cfg` binding describes the SiFli HPSYS_CFG block, which the SoC
devicetree declares together with the ``syscon`` fallback compatible.

See :ref:`dt-phandles` for the phandle property types and :ref:`dt-bindings` for writing the
consumer binding.

Typical application flow
************************

The syscon API is normally called from another driver or from SoC code rather than from an
application. The sequence is the same in both cases:

#. Reference the syscon node from devicetree, either through a phandle property of the consumer
   node (:c:macro:`DT_INST_PHANDLE` or :c:macro:`DT_PHANDLE`) or directly through its node label
   (:c:macro:`DT_NODELABEL`), and obtain the device with :c:macro:`DEVICE_DT_GET`.
#. Check that the device is ready with :c:func:`device_is_ready`.
#. Read registers with :c:func:`syscon_read_reg`.
#. Modify registers with :c:func:`syscon_update_bits` when only some bits change, or with
   :c:func:`syscon_write_reg` when the whole register is replaced.
#. When code needs the raw region, for example to hand it to hardware-specific helpers, query
   :c:func:`syscon_get_base` and :c:func:`syscon_get_size`.

Basic Operation
***************

The example below follows the in-tree clock control and GPIO drivers that keep a reference to
their syscon device and use it to flip control bits. It sets one bit of a register and reads it
back. The register offset and bit are placeholders for values documented by the SoC:

.. code-block:: c
   :caption: Setting a bit in a syscon register

   #include <zephyr/device.h>
   #include <zephyr/devicetree.h>
   #include <zephyr/drivers/syscon.h>
   #include <zephyr/sys/util.h>

   #define CLK_GATE_REG   0x10U    /* byte offset from the syscon base */
   #define CLK_GATE_UART0 BIT(3)

   static const struct device *const sysctrl = DEVICE_DT_GET(DT_NODELABEL(sysctrl));

   int enable_uart0_clock(void)
   {
           uint32_t val;
           int ret;

           if (!device_is_ready(sysctrl)) {
                   return -ENODEV;
           }

           /* Set the gate bit, leaving the other bits of the register untouched */
           ret = syscon_update_bits(sysctrl, CLK_GATE_REG, CLK_GATE_UART0, CLK_GATE_UART0);
           if (ret < 0) {
                   return ret;
           }

           ret = syscon_read_reg(sysctrl, CLK_GATE_REG, &val);
           if (ret < 0) {
                   return ret;
           }

           return (val & CLK_GATE_UART0) != 0U ? 0 : -EIO;
   }

:c:func:`syscon_get_base` and :c:func:`syscon_get_size` follow the same pattern: they fill a
``uintptr_t`` and a ``size_t`` describing the register region the driver operates on.

Register offsets and bounds checking
====================================

Offsets are byte offsets from the start of the region, whatever the register width. The generic
driver validates every offset before touching the hardware and returns ``-EINVAL`` when the offset
is not a multiple of ``reg-io-width`` or is not below the region size. It then performs a single
8-, 16- or 32-bit bus access: narrower registers are read into the low bits of the 32-bit value and
written from them. The tests in :zephyr_file:`tests/drivers/syscon/src/main.c` exercise these
rules: on a region with ``reg-io-width = <4>`` accesses at offsets 0 and 4 succeed while offsets
1, 2, 3 and 5 are rejected, and an access at an offset equal to the region size fails.

:c:func:`syscon_read_reg` also returns ``-EINVAL`` when the ``val`` pointer is ``NULL``.

Locking and calling context
===========================

All syscon calls are synchronous and complete before returning; the API defines no callbacks and
no asynchronous variants. The generic driver guards each access with a per-device
:c:struct:`k_spinlock`, and :c:func:`syscon_update_bits` performs its read-modify-write while
holding that lock, so concurrent updates of different bits of the same register do not lose
changes. The ELAN EM32 clock control driver, for example, relies on this and adds no locking of
its own around its clock gate updates.

The lock only covers one call. A sequence of accesses that must not be interleaved, such as
disabling a write protection key, updating a peripheral and enabling the key again, needs a lock
owned by the caller, as done in :zephyr_file:`drivers/rtc/rtc_mchp_g2.c`.

The generic driver never sleeps, so its operations can be used before the kernel is up and from
interrupt handlers. Another implementation of :c:struct:`syscon_driver_api` may impose its own
constraints.

Initialization order
====================

The generic driver initializes at the ``PRE_KERNEL_1`` level with the priority set by
:kconfig:option:`CONFIG_SYSCON_INIT_PRIORITY` (50 by default). During initialization it maps the
region with :c:macro:`DEVICE_MMIO_MAP`, so on systems with an MMU the address returned by
:c:func:`syscon_get_base` is the mapped address rather than the physical address from ``reg``.
Drivers that access syscon registers from their own initialization function must run after the
syscon device. In-tree consumers enforce this at build time, for example:

.. code-block:: c

   BUILD_ASSERT(CONFIG_SYSCON_INIT_PRIORITY < CONFIG_CLOCK_CONTROL_EM32_AHB_INIT_PRIORITY,
                "AHB clock controller must initialize after syscon");

Use by other drivers
********************

The syscon API is a building block for other driver classes rather than an end-user interface.
In-tree consumers include:

* :ref:`clock control <clock_control_api>` and :ref:`reset <reset_api>` drivers for the ASPEED
  AST10x0 family, whose ``sysclk`` and ``sysrst`` nodes are children of the syscon node
* :ref:`GPIO <gpio_api>`, :ref:`PWM <pwm_api>`, counter, serial and entropy drivers for the
  NEORV32, which read the SYSINFO block to check that the peripheral is implemented
* the :ref:`pinctrl <pinctrl_api>` driver for the Xilinx Zynq-7000 SLCR
* the :ref:`hwinfo <hwinfo_api>` driver for Andes SoCs, which reads the device version and the
  reset cause from the SMU
* :ref:`regulator <regulator_api>` and :ref:`OTP <otp_api>` drivers for the Realtek RTS5817, which
  build on :c:func:`syscon_update_bits`

The Kconfig entry of a consumer driver selects or depends on :kconfig:option:`CONFIG_SYSCON` so
that the class is enabled together with the consumer.

A syscon driver fills a :c:struct:`syscon_driver_api` and registers it with :c:macro:`DEVICE_API`.
Any of :c:member:`syscon_driver_api.read`, :c:member:`syscon_driver_api.write`,
:c:member:`syscon_driver_api.update_bits`, :c:member:`syscon_driver_api.get_base` and
:c:member:`syscon_driver_api.get_size` may be left ``NULL``. Registering through
:c:macro:`DEVICE_API` also makes the device recognizable with :c:macro:`DEVICE_API_IS`, which the
shell uses to offer only syscon devices for command completion.

Shell commands
**************

When :kconfig:option:`CONFIG_SYSCON_SHELL` is enabled, a set of ``syscon`` commands is available
in the :ref:`shell <shell_api>`. They allow inspecting and modifying syscon registers
interactively, for example to check the effect of a clock gate or pad setting without rebuilding
the application.

Each subcommand takes the syscon device name as its first argument; tab completion lists only the
devices that implement the syscon API. Addresses are byte offsets within the region. Addresses and
values accept decimal or ``0x``-prefixed hexadecimal notation and must fit in 32 bits.

The following subcommands are available:

``syscon base <device>``
  Print the base address of the register region in hexadecimal.

``syscon read <device> <address>``
  Read the register at byte offset ``address`` and print its value in hexadecimal.

``syscon write <device> <address> <value>``
  Write ``value`` to the register at byte offset ``address``. Nothing is printed on success.

``syscon size <device>``
  Print the size of the register region in bytes.

Failed API calls are reported with their negative return code, so a misaligned or out-of-range
offset shows up as ``-22`` (``-EINVAL``).

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_SYSCON`
* :kconfig:option:`CONFIG_SYSCON_GENERIC`
* :kconfig:option:`CONFIG_SYSCON_INIT_PRIORITY`
* :kconfig:option:`CONFIG_SYSCON_SHELL`

API Reference
*************

.. doxygengroup:: syscon_interface
