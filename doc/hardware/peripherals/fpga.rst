.. _fpga_api:

FPGA
####

Overview
********

A Field-Programmable Gate Array (FPGA) is a device whose logic is defined by a configuration image,
called a bitstream, that is loaded into the device after power-up. Zephyr drives several kinds of
programmable logic through the same interface: FPGA fabrics embedded in a SoC (QuickLogic EOS S3,
Xilinx ZynqMP, Microchip PolarFire SoC), external FPGAs configured over SPI (Lattice iCE40, Renesas
SLG47910), GreenPAK mixed-signal ICs configured over I2C (Renesas SLG47105 and SLG47115), and the
bridges between the processor system and the fabric of Intel Agilex SoCs.

The FPGA API abstracts the lifecycle of such a device rather than the design running inside it:
powering the device on and off, resetting it, loading a bitstream from memory, and querying its
programming status and a short description. Once a design is loaded, the application interacts
with it through whatever interface the design exposes, which is outside the scope of this API. Key
concepts include:

**FPGA device**
  A :c:struct:`device` whose driver implements :c:struct:`fpga_driver_api`. Every function of the
  API takes the device pointer, usually obtained with :c:macro:`DEVICE_DT_GET`. All driver
  callbacks are optional: an operation the driver does not provide fails with ``-ENOTSUP``, and
  the two getters fall back to :c:enumerator:`FPGA_STATUS_INACTIVE` and
  :c:macro:`FPGA_GET_INFO_DEFAULT`.

**Bitstream**
  The configuration image, passed to :c:func:`fpga_load` as a pointer to ``uint32_t`` words
  together with its size in bytes. The API does not define the image format: each driver expects
  the format produced by the vendor toolchain for its device.

**Programming status** (:c:enum:`FPGA_status`)
  :c:func:`fpga_get_status` reports whether the device can accept a bitstream
  (:c:enumerator:`FPGA_STATUS_ACTIVE`) or not (:c:enumerator:`FPGA_STATUS_INACTIVE`).

**Power and reset control**
  :c:func:`fpga_on` and :c:func:`fpga_off` switch the device or its power domain on and off, and
  :c:func:`fpga_reset` brings it back to a state in which a new bitstream can be loaded.

**Device information**
  :c:func:`fpga_get_info` returns a driver-specific, human-readable string, such as the detected
  part name or the CRC of the loaded image, or :c:macro:`FPGA_GET_INFO_DEFAULT` when the driver
  provides nothing.

Bitstreams and programming status
*********************************

:c:func:`fpga_load` consumes the whole bitstream from memory and returns once the device has been
programmed, or with an error when programming failed. Applications typically link the image into
the firmware as a ``uint32_t`` array generated from the vendor toolchain output, as the
:zephyr:code-sample:`fpga-controller` sample does, or load it into RAM at run time (for example
with the ``devmem load`` shell command) and pass its address. Where the hardware allows it,
drivers verify the result before returning: the iCE40 drivers check the ``CDONE`` signal after
the transfer, and the SLG471x5 driver reads the configuration registers back.

Drivers for SoC-internal fabrics derive :c:enum:`FPGA_status` from hardware registers, for example
the ``PL_INIT`` and ``PL_DONE`` flags of the ZynqMP configuration interface, while drivers for
externally attached devices (iCE40, SLG471x5, SLG47910) track the state in software and report
:c:enumerator:`FPGA_STATUS_ACTIVE` only after a bitstream has been loaded successfully (and, for
the iCE40, while the device is turned on). The status is therefore a coarse readiness indication;
the return value of :c:func:`fpga_load` tells whether programming succeeded.

:c:func:`fpga_reset` is the way to program a device that already holds a design: the sample
alternates between two bitstreams by resetting the fabric between the loads. What a reset does is
driver specific, from pulsing the ``CRESET_B`` line of an iCE40 to power cycling the EOS S3 fabric.

Supported drivers
*****************

The drivers live in :zephyr_file:`drivers/fpga`. Not all of them implement the whole API.

**Lattice iCE40** (:kconfig:option:`CONFIG_ICE40_FPGA`)
  Programs an iCE40 in SPI slave configuration mode. The :kconfig:option:`CONFIG_ICE40_FPGA_SPI`
  variant (:dtcompatible:`lattice,ice40-fpga`) uses the :ref:`SPI API <spi_api>` and requires a
  GPIO chip select, which it releases while the leading and trailing clocks are sent. The
  experimental :kconfig:option:`CONFIG_ICE40_FPGA_BITBANG` variant
  (:dtcompatible:`lattice,ice40-fpga-bitbang`) bit-bangs the clock and data lines through direct
  GPIO register writes for microcontrollers that cannot meet the timing requirements between
  separate SPI transfers. Both implement all operations and report the CRC32 of the loaded image
  through :c:func:`fpga_get_info`.

**QuickLogic EOS S3** (:kconfig:option:`CONFIG_EOS_S3_FPGA`)
  Drives the embedded FPGA fabric of the EOS S3 SoC through its configuration registers and binds
  to the node labeled ``fpga0`` in the board devicetree. All operations are implemented; this is
  the driver used by the :zephyr:code-sample:`fpga-controller` sample.

**Xilinx ZynqMP** (:kconfig:option:`CONFIG_ZYNQMP_FPGA`, :dtcompatible:`xlnx,fpga`)
  Programs the programmable logic through the PCAP interface and the CSU DMA. The driver expects a
  bitstream carrying the Xilinx header (design name, part name, date and time sections), which it
  skips, and reports the detected part name (``ZU2`` to ``ZU49``) through :c:func:`fpga_get_info`.
  Power control is not implemented.

**Microchip PolarFire SoC** (:kconfig:option:`CONFIG_MPFS_FPGA`)
  Uses the system controller services and binds to the ``microchip,mpfs-mailbox`` node.
  :c:func:`fpga_load` writes the new design into the SPI flash referenced by the
  ``bitstream-flash`` devicetree alias, using the :ref:`flash API <flash_api>`, and
  :c:func:`fpga_reset` then asks the system controller to authenticate that image and program the
  fabric from it. :c:func:`fpga_get_info` reports the design version. Power control is not
  implemented.

**Intel Agilex HPS to FPGA bridges** (:kconfig:option:`CONFIG_ALTERA_AGILEX_BRIDGE_FPGA`)
  Does not load bitstreams. :c:func:`fpga_on` and :c:func:`fpga_off` enable and disable the
  bridges between the hard processor system and the fabric by sending mailbox commands to the
  Secure Device Manager (SDM) through the Arm SiP services subsystem
  (:kconfig:option:`CONFIG_ARM_SIP_SVC_SUBSYS`), after checking that the fabric is configured.
  The driver binds to the ``bridges`` node with the ``altr,socfpga-agilex-bridge`` compatible.

**Renesas SLG47105 and SLG47115** (:kconfig:option:`CONFIG_SLG471X5_FPGA`)
  GreenPAK configurable mixed-signal ICs programmed over I2C (:dtcompatible:`renesas,slg47105`,
  :dtcompatible:`renesas,slg47115`). :c:func:`fpga_load` writes the register image (truncated to
  256 bytes) and reads selected register ranges back to verify it; with the ``try-unconfigured``
  property, the driver first tries the unconfigured I2C address ``0x00``. Only status, reset and
  load are implemented.

**Renesas SLG47910** (:kconfig:option:`CONFIG_SLG47910_FPGA`, :dtcompatible:`renesas,slg47910`)
  SPI-configured FPGA. The driver power cycles the device through ``pwr-gpios`` and ``en-gpios``,
  drives ``ss-gpios`` itself to enter configuration mode, then streams the bitstream over SPI.
  Only status, reset and load are implemented.

Devicetree Configuration
************************

FPGA fabrics embedded in a SoC are described by a single node, such as the ``fpga0`` node with the
:dtcompatible:`xlnx,fpga` compatible in :zephyr_file:`boards/enclustra/mercury_xu/mercury_xu.dts`.
Externally attached devices are children of the bus used to configure them and reference the GPIOs
that carry the control signals, as in this node from
:zephyr_file:`boards/vicharak/shrike_lite/shrike_lite.dts`:

.. code-block:: devicetree

   &spi0 {
       status = "okay";

       fpga: fpga@0 {
           compatible = "renesas,slg47910";
           reg = <0>;
           spi-max-frequency = <1600000>;

           pwr-gpios = <&gpio0 12 GPIO_ACTIVE_HIGH>;
           en-gpios = <&gpio0 13 GPIO_ACTIVE_HIGH>;
           ss-gpios = <&gpio0 1 GPIO_ACTIVE_HIGH>;
       };
   };

An iCE40 node needs ``cdone-gpios`` and ``creset-gpios`` for the ``CDONE`` and ``CRESET_B``
signals, and its SPI controller must provide a GPIO chip select through ``cs-gpios``. The
configuration timing can be tuned with ``creset-delay-us``, ``config-delay-us``,
``leading-clocks`` and ``trailing-clocks``, whose defaults follow the datasheet, and
``spi-max-frequency`` must lie between 1 MHz and 25 MHz. The bit-bang variant also takes
``clk-gpios``, ``pico-gpios``, ``gpios-set-reg``, ``gpios-clear-reg`` and a calibrated
``mhz-delay-count``. See the bindings in :zephyr_file:`dts/bindings/fpga` for all properties.

Typical application flow
************************

#. Get the FPGA device, usually with :c:macro:`DEVICE_DT_GET`, and check it with
   :c:func:`device_is_ready`.
#. Make the bitstream available in memory, either linked into the application image or loaded at
   run time.
#. Check :c:func:`fpga_get_status` and, when the device reports
   :c:enumerator:`FPGA_STATUS_INACTIVE`, call :c:func:`fpga_on` to power it up.
#. Program the device with :c:func:`fpga_load`, passing the address of the bitstream and its size
   in bytes, and check the return value.
#. To load another design, call :c:func:`fpga_reset` and then :c:func:`fpga_load` again. Call
   :c:func:`fpga_off` when the fabric is no longer needed and the driver supports power control.

Basic Operation
***************

The :zephyr:code-sample:`fpga-controller` sample stores two bitstreams generated by the QuickLogic
toolchain as ``uint32_t`` arrays in header files and alternates between them. The following
function programs a device with such an array, powering it up first when necessary:

.. code-block:: c
   :caption: Loading a bitstream into an FPGA

   #include <zephyr/device.h>
   #include <zephyr/drivers/fpga.h>
   #include <zephyr/sys/printk.h>

   static const struct device *const fpga = DEVICE_DT_GET(DT_NODELABEL(fpga0));

   static int program_fpga(uint32_t *bitstream, uint32_t size)
   {
       int ret;

       if (!device_is_ready(fpga)) {
           return -ENODEV;
       }

       if (fpga_get_status(fpga) == FPGA_STATUS_INACTIVE) {
           ret = fpga_on(fpga);
           /* Power control is optional; the driver may not implement it */
           if (ret < 0 && ret != -ENOTSUP) {
               return ret;
           }
       }

       ret = fpga_load(fpga, bitstream, size);
       if (ret != 0) {
           return ret;
       }

       printk("%s programmed: %s\n", fpga->name, fpga_get_info(fpga));

       return 0;
   }

Blocking behavior and constraints
*********************************

* All API calls are synchronous and return only when the operation has completed; there is no
  asynchronous variant and no completion callback. Drivers wait with busy loops, sleeps,
  semaphores and mutexes while they program the device (the iCE40 drivers run the transfer from
  the system work queue and block the caller on a semaphore, and the SLG47910 driver sleeps for
  several hundred milliseconds while it power cycles the device), so the API must be called from
  thread context and never from an interrupt handler.
* The bitstream buffer is read directly by the driver and must stay valid and unchanged until the
  call returns. Some drivers consume it as 32-bit words, so it should be word aligned.
* The API itself does not serialize access to a device. Some drivers protect their state with a
  mutex or a spinlock, but applications that share an FPGA device between threads should
  serialize their calls.
* Successful calls return ``0``. Operations a driver does not implement return ``-ENOTSUP``,
  except that :c:func:`fpga_get_status` returns :c:enumerator:`FPGA_STATUS_INACTIVE` and
  :c:func:`fpga_get_info` returns :c:macro:`FPGA_GET_INFO_DEFAULT` (``"n/a"``). Treat any other
  nonzero value as a failure.
* Power control is explicit: the drivers do not integrate with the device power management
  subsystem, and the application calls :c:func:`fpga_on` and :c:func:`fpga_off` where the
  hardware supports them.

Shell commands
**************

When :kconfig:option:`CONFIG_FPGA_SHELL` is enabled, a set of ``fpga`` commands is available. They
allow programming and inspecting a device from the :ref:`shell <shell_api>` without writing
application code; the :zephyr:code-sample:`fpga-controller` sample provides a ``prj_shell.conf``
configuration that enables them and prints the address and size of its built-in bitstreams.

Each subcommand takes the FPGA device name as its first argument. The following subcommands are
available:

``fpga on <device>``
  Turn the FPGA on with :c:func:`fpga_on`.

``fpga off <device>``
  Turn the FPGA off with :c:func:`fpga_off`.

``fpga reset <device>``
  Reset the FPGA with :c:func:`fpga_reset`.

``fpga load <device> <address> <size in bytes>``
  Program the FPGA with :c:func:`fpga_load` using the bitstream found at ``address`` in memory.
  The address is given in decimal, or in hexadecimal with a ``0x`` prefix, and the size in bytes.

``fpga get_status <device>``
  Print the numeric value returned by :c:func:`fpga_get_status`: ``0`` for
  :c:enumerator:`FPGA_STATUS_INACTIVE` and ``1`` for :c:enumerator:`FPGA_STATUS_ACTIVE`.

``fpga get_info <device>``
  Print the string returned by :c:func:`fpga_get_info`.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_FPGA`
* :kconfig:option:`CONFIG_FPGA_SHELL`
* :kconfig:option:`CONFIG_FPGA_INIT_PRIORITY`
* :kconfig:option:`CONFIG_ICE40_FPGA`
* :kconfig:option:`CONFIG_ICE40_FPGA_SPI`
* :kconfig:option:`CONFIG_ICE40_FPGA_BITBANG`
* :kconfig:option:`CONFIG_EOS_S3_FPGA`
* :kconfig:option:`CONFIG_ZYNQMP_FPGA`
* :kconfig:option:`CONFIG_MPFS_FPGA`
* :kconfig:option:`CONFIG_ALTERA_AGILEX_BRIDGE_FPGA`
* :kconfig:option:`CONFIG_SLG471X5_FPGA`
* :kconfig:option:`CONFIG_SLG47910_FPGA`

The drivers log through the :ref:`logging subsystem <logging_api>` under the ``FPGA`` log module,
whose level is selected with the ``CONFIG_FPGA_LOG_LEVEL`` family of options.

API Reference
*************

.. doxygengroup:: fpga_interface
