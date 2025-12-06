.. _fpga_api:

Field-Programmable Gate Array (FPGA)
#####################################

Overview
********

A Field-Programmable Gate Array (FPGA) is an integrated circuit that can be
programmed or reprogrammed after manufacturing to implement custom digital
logic. FPGAs are used in embedded systems to provide hardware acceleration,
implement custom peripherals, or add functionality that is not available in
standard microcontrollers.

The FPGA API provides a generic method to interact with FPGA devices. It allows
applications to program, configure, and control FPGA devices. Key features include:

**Bitstream Loading**
  Load configuration bitstreams to program the FPGA with custom logic designs.

**Device Control**
  Power the FPGA on or off, reset the device, and check its status.

**Status Monitoring**
  Query whether the FPGA is in an active state (ready to be programmed) or
  inactive state.

**Device Information**
  Retrieve information about the FPGA device, such as its type and capabilities.

Devicetree Configuration
************************

FPGA devices are defined in the Devicetree with a compatible string that
identifies the specific FPGA controller driver. The exact properties depend on
the FPGA vendor and device type.

Example of a simple FPGA device definition:

.. code-block:: devicetree

   fpga0: fpga {
       compatible = "xlnx,fpga";
       status = "okay";
   };

Example of a Lattice iCE40 FPGA using SPI:

.. code-block:: devicetree

   &spi0 {
       status = "okay";
       
       fpga: fpga@0 {
           compatible = "lattice,ice40-fpga";
           reg = <0>;
           spi-max-frequency = <1000000>;
           cdone-gpios = <&gpio0 10 0>;
           creset-gpios = <&gpio0 11 GPIO_PUSH_PULL>;
           bus-width = <1>;
       };
   };

Example of a Lattice iCE40 FPGA using GPIO bitbang:

.. code-block:: devicetree

   fpga: fpga {
       compatible = "lattice,ice40-fpga-bitbang";
       cdone-gpios = <&gpio0 15 0>;
       creset-gpios = <&gpio0 16 GPIO_PUSH_PULL>;
       clk-gpios = <&gpio0 17 GPIO_PUSH_PULL>;
       pico-gpios = <&gpio0 18 GPIO_PUSH_PULL>;
       gpios-set-reg = <0x60004008>;
       gpios-clear-reg = <0x6000400c>;
   };

Example of referencing an FPGA device:

.. code-block:: devicetree

   / {
       chosen {
           zephyr,fpga = &fpga0;
       };
       
       aliases {
           fpga0 = &fpga0;
       };
   };

Basic Operation
***************

FPGA operations are performed on a device structure obtained using standard
Zephyr device APIs.

.. code-block:: c
   :caption: Getting the FPGA device and checking if it's ready

   #include <zephyr/device.h>
   #include <zephyr/drivers/fpga.h>
   
   const struct device *fpga_dev = DEVICE_DT_GET(DT_NODELABEL(fpga0));
   
   if (!device_is_ready(fpga_dev)) {
       printk("FPGA device is not ready\n");
       return -ENODEV;
   }

Loading a Bitstream
===================

The primary operation with an FPGA is loading a bitstream to configure its
logic. The bitstream data is typically stored in flash memory or included
in the application binary.

.. code-block:: c
   :caption: Loading a bitstream into the FPGA

   #include <zephyr/drivers/fpga.h>
   
   /* Bitstream data (example - typically loaded from file or embedded) */
   extern uint32_t fpga_bitstream[];
   extern uint32_t fpga_bitstream_size;
   
   int ret;
   
   /* Check FPGA status before loading */
   enum FPGA_status status = fpga_get_status(fpga_dev);
   if (status != FPGA_STATUS_ACTIVE) {
       printk("FPGA is not in active state\n");
       return -EBUSY;
   }
   
   /* Load the bitstream */
   ret = fpga_load(fpga_dev, fpga_bitstream, fpga_bitstream_size);
   if (ret < 0) {
       printk("Failed to load bitstream: %d\n", ret);
       return ret;
   }
   
   printk("Bitstream loaded successfully\n");

Resetting the FPGA
==================

After loading a bitstream or to prepare for a new configuration, you may need
to reset the FPGA.

.. code-block:: c
   :caption: Resetting the FPGA device

   int ret = fpga_reset(fpga_dev);
   if (ret < 0) {
       printk("Failed to reset FPGA: %d\n", ret);
       return ret;
   }
   
   printk("FPGA reset successfully\n");

Power Control
=============

Some FPGA controllers support power management operations to turn the FPGA
on or off.

.. code-block:: c
   :caption: Turning the FPGA on and off

   /* Turn on the FPGA */
   int ret = fpga_on(fpga_dev);
   if (ret < 0) {
       printk("Failed to turn on FPGA: %d\n", ret);
       return ret;
   }
   
   /* Perform operations with FPGA... */
   
   /* Turn off the FPGA to save power */
   ret = fpga_off(fpga_dev);
   if (ret < 0) {
       printk("Failed to turn off FPGA: %d\n", ret);
       return ret;
   }

Checking Device Status and Information
=======================================

You can query the FPGA's status and retrieve device information.

.. code-block:: c
   :caption: Querying FPGA status and information

   /* Get FPGA status */
   enum FPGA_status status = fpga_get_status(fpga_dev);
   if (status == FPGA_STATUS_ACTIVE) {
       printk("FPGA is active and ready for programming\n");
   } else {
       printk("FPGA is inactive\n");
   }
   
   /* Get device information */
   const char *info = fpga_get_info(fpga_dev);
   printk("FPGA Info: %s\n", info);

Complete Example
================

Refer to :zephyr:code-sample:`fpga-controller` for a complete example of FPGA
operations including bitstream loading, reset operations, and shell integration.

FPGA Shell
**********

When :kconfig:option:`CONFIG_FPGA_SHELL` is enabled, the FPGA subsystem provides
a shell interface for interacting with FPGA devices. This is useful for testing
and debugging FPGA operations.

Available shell commands:

``fpga load <device> <address> <size>``
  Load a bitstream from the specified memory address into the FPGA device.
  
  Example:
  
  .. code-block:: console
  
     uart:~$ fpga load fpga0 0x20010000 75960
     FPGA: loading bitstream

``fpga reset <device>``
  Reset the specified FPGA device.
  
  Example:
  
  .. code-block:: console
  
     uart:~$ fpga reset fpga0
     FPGA: resetting FPGA

``fpga status <device>``
  Display the status of the specified FPGA device.
  
  Example:
  
  .. code-block:: console
  
     uart:~$ fpga status fpga0
     FPGA status: ACTIVE

``fpga info <device>``
  Display information about the specified FPGA device.
  
  Example:
  
  .. code-block:: console
  
     uart:~$ fpga info fpga0
     FPGA Info: Xilinx Zynq UltraScale+ MPSoC

``fpga on <device>``
  Turn on the specified FPGA device (if supported by the driver).

``fpga off <device>``
  Turn off the specified FPGA device (if supported by the driver).

Supported FPGA Devices
**********************

Zephyr supports various FPGA vendors and device families:

Lattice iCE40
=============

The Lattice iCE40 family of FPGAs is supported through two drivers:

- **SPI-based driver** (:dtcompatible:`lattice,ice40-fpga`): Uses SPI to
  transfer the bitstream to the FPGA.
- **GPIO bitbang driver** (:dtcompatible:`lattice,ice40-fpga-bitbang`): Uses
  GPIO pins to bitbang the SPI protocol when hardware SPI is not available.

Key features:

- Configuration via SPI interface or GPIO bitbanging
- Support for CDONE (Configuration Done) and CRESET (Configuration Reset) pins
- Configurable timing parameters for reset and configuration delays
- Support for leading and trailing clock cycles during programming

Enable with :kconfig:option:`CONFIG_FPGA_ICE40`.

Xilinx FPGAs
============

Xilinx FPGA devices are supported including:

- **Zynq UltraScale+ MPSoC** (:dtcompatible:`xlnx,fpga`): Supports bitstream
  loading through the Platform Management Unit (PMU) interface.

Enable with :kconfig:option:`CONFIG_FPGA_ZYNQMP`.

Intel/Altera FPGAs
==================

Intel (formerly Altera) FPGA devices are supported including:

- **Agilex SoC FPGA Bridge** (:kconfig:option:`CONFIG_FPGA_ALTERA_AGILEX_BRIDGE`):
  Supports configuring FPGA-to-HPS and HPS-to-FPGA bridges on Intel Agilex SoCs.
- **Microchip PolarFire SoC** (:kconfig:option:`CONFIG_FPGA_MPFS`): Supports
  bitstream loading on Microchip PolarFire SoC FPGAs.

QuickLogic FPGAs
================

QuickLogic FPGA devices are supported:

- **EOS S3** (:kconfig:option:`CONFIG_FPGA_EOS_S3`): Supports the embedded FPGA
  in QuickLogic EOS S3 SoCs, as found on the QuickFeather board.

Renesas GreenPAK
================

Renesas (formerly Silego) configurable mixed-signal ICs:

- **SLG47105/SLG47115** (:dtcompatible:`renesas,slg471x5`): Small programmable
  mixed-signal matrix devices configured via I2C.

Enable with :kconfig:option:`CONFIG_FPGA_SLG471X5`.

Configuration Options
*********************

Main configuration options:

* :kconfig:option:`CONFIG_FPGA` - Enable FPGA driver support
* :kconfig:option:`CONFIG_FPGA_SHELL` - Enable FPGA shell commands
* :kconfig:option:`CONFIG_FPGA_INIT_PRIORITY` - Set FPGA driver initialization priority

Vendor-specific options:

* :kconfig:option:`CONFIG_FPGA_ICE40` - Lattice iCE40 FPGA support
* :kconfig:option:`CONFIG_FPGA_ZYNQMP` - Xilinx Zynq UltraScale+ MPSoC FPGA support
* :kconfig:option:`CONFIG_FPGA_ALTERA_AGILEX_BRIDGE` - Intel Agilex FPGA bridge support
* :kconfig:option:`CONFIG_FPGA_MPFS` - Microchip PolarFire SoC FPGA support
* :kconfig:option:`CONFIG_FPGA_EOS_S3` - QuickLogic EOS S3 FPGA support
* :kconfig:option:`CONFIG_FPGA_SLG471X5` - Renesas SLG471x5 GreenPAK support

API Reference
*************

.. doxygengroup:: fpga_interface
