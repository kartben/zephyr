.. _bl5340_dvk:

Ezurio BL5340 DVK
#################

Overview
********
The BL5340 Development Kit provides support for the Ezurio
BL5340 module which is powered by a dual-core Nordic Semiconductor
nRF5340 ARM Cortex-M33F CPU. The nRF5340 inside the BL5340 module is a
dual-core SoC based on the Arm® Cortex®-M33 architecture, with:

* a full-featured Arm Cortex-M33F core with DSP instructions, FPU, and
  Armv8-M Security Extension, running at up to 128 MHz, referred to as
  the **application core**
* a secondary Arm Cortex-M33 core, with a reduced feature set, running
  at a fixed 64 MHz, referred to as the **network core**.

The ``bl5340_dvk/nrf5340/cpuapp`` build target provides support for the application
core on the BL5340 module. The ``bl5340_dvk/nrf5340/cpunet`` build target provides
support for the network core on the BL5340 module. If ARM TrustZone is
used then the ``bl5340_dvk/nrf5340/cpuapp`` build target provides support for the
non-secure partition of the application core on the BL5340 module.

This development kit has the following features:

* :abbr:`ADC (Analog to Digital Converter)`
* CLOCK
* FLASH
* :abbr:`GPIO (General Purpose Input Output)`
* :abbr:`IDAU (Implementation Defined Attribution Unit)`
* :abbr:`I2C (Inter-Integrated Circuit)`
* :abbr:`I2S (Inter-Integrated Sound)`
* :abbr:`MPU (Memory Protection Unit)`
* :abbr:`NVIC (Nested Vectored Interrupt Controller)`
* :abbr:`PWM (Pulse Width Modulation)`
* :abbr:`QSPI (Quad Serial Peripheral Interface)`
* RADIO (Bluetooth Low Energy and 802.15.4)
* :abbr:`RTC (nRF RTC System Clock)`
* Segger RTT (RTT Console)
* :abbr:`SPI (Serial Peripheral Interface)`
* :abbr:`UARTE (Universal asynchronous receiver-transmitter)`
* :abbr:`USB (Universal Serial Bus)`
* :abbr:`WDT (Watchdog Timer)`

.. figure:: img/bl5340_dvk_top.jpg
     :align: center
     :alt: BL5340 DVK

     BL5340 DVK (Credit: Ezurio)

More information about the module can be found on the
`BL5340 homepage`_.

The `Nordic Semiconductor Infocenter`_
contains the processor's information and the datasheet.

Hardware
********

The BL5340 DVK has two external oscillators. The frequency of
the slow clock is 32.768KHz. The frequency of the main clock
is 32MHz.

Supported Features
==================

The ``bl5340_dvk/nrf5340/cpuapp`` board configuration supports the following
hardware features:

.. list-table:: Enabled features
   :widths: 20 80
   :header-rows: 1

   * - Type
     - Compatible
   * - MIPI-DBI
     - * MIPI-DBI Mode C compatible SPI controller. This driver emulates MIPI DBI (:dtcompatible:`zephyr,mipi-dbi-spi`)
   * - POWER
     - * Nordic nRF power control node (:dtcompatible:`nordic,nrf-power`)
       * Nordic nRF family USBREG (USB Regulator Control) (:dtcompatible:`nordic,nrf-usbreg`)
       * Nordic VMC (Volatile Memory Controller) (:dtcompatible:`nordic,nrf-vmc`)
   * - FLASH_CONTROLLER
     - * Properties defining the interface for the Nordic QSPI peripheral. (:dtcompatible:`nordic,nrf-qspi`)
       * Nordic NVMC (Non-Volatile Memory Controller) (:dtcompatible:`nordic,nrf53-flash-controller`)
   * - MISC
     - * Nordic FICR (Factory Information Configuration Registers) (:dtcompatible:`nordic,nrf-ficr`)
       * Nordic DPPIC (Distributed Programmable Peripheral Interconnect Controller) (:dtcompatible:`nordic,nrf-dppic`)
   * - SD
     - * Generic Zephyr SPI based SDHC controller (:dtcompatible:`zephyr,sdhc-spi-slot`)
       * Zephyr MMC disk node. A binding with this compatible present within an SD (:dtcompatible:`zephyr,sdmmc-disk`)
   * - PINCTRL
     - * The nRF pin controller is a singleton node responsible for controlling (:dtcompatible:`nordic,nrf-pinctrl`)
   * - PWM
     - * nRF PWM (:dtcompatible:`nordic,nrf-pwm`)
   * - LED
     - * This allows you to define a group of LEDs. Each LED in the group is (:dtcompatible:`gpio-leds`)
   * - CPU
     - * ARM Cortex-M33F CPU (:dtcompatible:`arm,cortex-m33f`)
   * - INTERRUPT-CONTROLLER
     - * ARMv8-M NVIC (Nested Vectored Interrupt Controller) (:dtcompatible:`arm,v8m-nvic`)
   * - RETAINED_MEM
     - * Nordic GPREGRET (General Purpose Register Retention) device. (:dtcompatible:`nordic,nrf-gpregret`)
   * - INPUT
     - * FT3267/FT5XX6/FT6XX6 capacitive touch panels (:dtcompatible:`focaltech,ft5336`)
       * Zephyr Input GPIO KEYS parent node (:dtcompatible:`gpio-keys`)
       * LVGL pointer indev pseudo-device (:dtcompatible:`zephyr,lvgl-pointer-input`)
   * - I2C
     - * Nordic nRF family TWIM (TWI master with EasyDMA). (:dtcompatible:`nordic,nrf-twim`)
   * - REGULATOR
     - * Nordic REGULATORS (voltage regulators control module) on nRF53X. (:dtcompatible:`nordic,nrf53x-regulators`)
       * Nordic nRF5X regulator (fixed stage of the core supply) (:dtcompatible:`nordic,nrf5x-regulator`)
       * Nordic nRF53X regulator (high voltage stage of the main supply) (:dtcompatible:`nordic,nrf53x-regulator-hv`)
   * - ETHERNET
     - * ENC424J600 standalone 100BASE-T Ethernet controller with SPI interface (:dtcompatible:`microchip,enc424j600`)
   * - MBOX
     - * Nordic nRF family IPC (MBOX Interprocessor Communication) (:dtcompatible:`nordic,mbox-nrf-ipc`)
   * - USB
     - * Nordic nRF52 USB device controller (:dtcompatible:`nordic,nrf-usbd`)
   * - SPI
     - * Nordic nRF family SPIM (SPI master with EasyDMA) (:dtcompatible:`nordic,nrf-spim`)
   * - WATCHDOG
     - * Nordic nRF family WDT (Watchdog Timer) (:dtcompatible:`nordic,nrf-wdt`)
   * - ADC
     - * Nordic Semiconductor nRF family SAADC node (:dtcompatible:`nordic,nrf-saadc`)
   * - SRAM
     - * Generic on-chip SRAM description (:dtcompatible:`mmio-sram`)
   * - BLUETOOTH
     - * Bluetooth module that uses Zephyr's Bluetooth Host Controller Interface as (:dtcompatible:`zephyr,bt-hci-entropy`)
       * Bluetooth HCI using the IPC subsystem (:dtcompatible:`zephyr,bt-hci-ipc`)
   * - RTC
     - * Microchip MCP7940N I2C RTC with battery-backed SRAM (:dtcompatible:`microchip,mcp7940n`)
   * - SDHC
     - * Generic Zephyr SPI based SDHC controller (:dtcompatible:`zephyr,sdhc-spi-slot`)
   * - MM
     - * ARMv8-M MPU (Memory Protection Unit) (:dtcompatible:`arm,armv8m-mpu`)
   * - IPC
     - * OpenAMP (RPMsg with static VRINGs) backend (:dtcompatible:`zephyr,ipc-openamp-static-vrings`)
   * - DISPLAY
     - * ILI9340 320x240 display controller (:dtcompatible:`ilitek,ili9340`)
   * - DAC
     - * Microchip MCP4725 12-bit DAC (:dtcompatible:`microchip,mcp4725`)
   * - MMU_MPU
     - * ARMv8-M MPU (Memory Protection Unit) (:dtcompatible:`arm,armv8m-mpu`)
   * - ARM
     - * Nordic UICR (User Information Configuration Registers) (:dtcompatible:`nordic,nrf-uicr`)
       * Nordic nRF family DCNF (Domain Configuration) (:dtcompatible:`nordic,nrf-dcnf`)
       * Nordic nRF family RESET (Reset Control) (:dtcompatible:`nordic,nrf-reset`)
       * Nordic nRF family CTRL-AP (Control Access Port) (:dtcompatible:`nordic,nrf-ctrlapperi`)
       * Nordic EGU (Event Generator Unit) (:dtcompatible:`nordic,nrf-egu`)
       * Nordic nRF family MUTEX (Mutual Exclusive Peripheral) (:dtcompatible:`nordic,nrf-mutex`)
       * Nordic KMU (Key Management Unit) (:dtcompatible:`nordic,nrf-kmu`)
       * Nordic SPU (System Protection Unit) (:dtcompatible:`nordic,nrf-spu`)
   * - MTD
     - * I2C EEPROMs compatible with Atmel's AT24 family (:dtcompatible:`atmel,at24`)
       * QSPI NOR flash supporting the JEDEC CFI interface. (:dtcompatible:`nordic,qspi-nor`)
       * This binding is used to describe fixed partitions of a flash (or (:dtcompatible:`fixed-partitions`)
       * Flash node (:dtcompatible:`soc-nv-flash`)
   * - GPIO
     - * TCA9538 GPIO node (:dtcompatible:`ti,tca9538`)
       * NRF5 GPIO node (:dtcompatible:`nordic,nrf-gpio`)
       * NRF5 GPIOTE node (:dtcompatible:`nordic,nrf-gpiote`)
       * This is an abstract device responsible for forwarding pins between cores. (:dtcompatible:`nordic,nrf-gpio-forwarder`)
   * - SERIAL
     - * Nordic nRF family UARTE (UART with EasyDMA) (:dtcompatible:`nordic,nrf-uarte`)
   * - CLOCK
     - * Nordic nRF family OSCILLATORS (Oscillator Control) (:dtcompatible:`nordic,nrf-oscillators`)
       * Nordic nRF clock control node (:dtcompatible:`nordic,nrf-clock`)
   * - DEBUG
     - * ARMv8 instrumentation trace macrocell. Used for single wire output (SWO) (:dtcompatible:`arm,armv8m-itm`)
   * - SENSOR
     - * STMicroelectronics LIS2DH 3-axis accelerometer accessed through I2C bus (:dtcompatible:`st,lis2dh`)
       * The BME680 is an integrated environmental sensor that measures (:dtcompatible:`bosch,bme680`)



The ``bl5340_dvk/nrf5340/cpunet`` board configuration supports the following
hardware features:

+-----------+------------+----------------------+
| Interface | Controller | Driver/Component     |
+===========+============+======================+
| CLOCK     | on-chip    | clock_control        |
+-----------+------------+----------------------+
| FLASH     | on-chip    | flash                |
+-----------+------------+----------------------+
| GPIO      | on-chip    | gpio                 |
+-----------+------------+----------------------+
| I2C(M)    | on-chip    | i2c                  |
+-----------+------------+----------------------+
| MPU       | on-chip    | arch/arm             |
+-----------+------------+----------------------+
| NVIC      | on-chip    | arch/arm             |
+-----------+------------+----------------------+
| RADIO     | on-chip    | Bluetooth,           |
|           |            | ieee802154           |
+-----------+------------+----------------------+
| RTC       | on-chip    | system clock         |
+-----------+------------+----------------------+
| RTT       | Segger     | console              |
+-----------+------------+----------------------+
| SPI(M/S)  | on-chip    | spi                  |
+-----------+------------+----------------------+
| UARTE     | on-chip    | serial               |
+-----------+------------+----------------------+
| WDT       | on-chip    | watchdog             |
+-----------+------------+----------------------+

Other hardware features have not been enabled yet for this board.
See `Nordic Semiconductor Infocenter`_
for a complete list of hardware features.

Connections and IOs
===================

An eight-pin GPIO port expander is used to provide additional inputs
and outputs to the BL5340 module.

Refer to the `TI TCA9538 datasheet`_ for further details.

LEDs
----

* LED1 (blue) = via TCA9538 port expander channel P4 (active low)
* LED2 (blue) = via TCA9538 port expander channel P5 (active low)
* LED3 (blue) = via TCA9538 port expander channel P6 (active low)
* LED4 (blue) = via TCA9538 port expander channel P7 (active low)

Push buttons
------------

* BUTTON1 = SW1 = via TCA9538 port expander channel P0 (active low)
* BUTTON2 = SW2 = via TCA9538 port expander channel P1 (active low)
* BUTTON3 = SW3 = via TCA9538 port expander channel P2 (active low)
* BUTTON4 = SW4 = via TCA9538 port expander channel P3 (active low)
* BOOT = boot (active low)

External Memory
===============

Several external memory sources are available for the BL5340 DVK. These
are described as follows.

Flash Memory
------------

A Macronix MX25R6435FZNIL0 8MB external QSPI Flash memory part is
incorporated for application image storage and large datasets.

Refer to the `Macronix MX25R6435FZNIL0 datasheet`_ for further details.

EEPROM Memory
-------------

A 32KB Giantec GT24C256C-2GLI-TR EEPROM is available via I2C for
storage of infrequently updated data and small datasets.

Refer to the `Giantec GT24C256C-2GLI-TR datasheet`_ for further details.

External Memory
---------------

An on-board micro SD card slot is available for use with micro SD cards.

Sensors
=======

The BL5340 DVK incorporates two sensors for user application testing.
These are described as follows.

Temperature, Pressure, Humidity & Air Quality Sensor
----------------------------------------------------

A Bosch BME680 Temperature, Pressure, Humidity & Air Quality sensor is
available via I2C for environmental measurement applications.

Refer to the `Bosch BME680 datasheet`_ for further details.

3-Axis Accelerometer
--------------------

An ST Microelectronics LIS3DH 3-Axis Accelerometer is available via I2C
for vibration and motion detection applications.

Refer to the `ST Microelectronics LIS3DH datasheet`_ for further details.

Ethernet
========

Cabled 10/100 Base-T Ethernet Connectivity is available via a Microchip
ENC424J600 Ethernet controller.

Refer to the `Microchip ENC424J600 datasheet`_ for further details.

TFT Display & Capacitive Touch Screen Controller
================================================

A 2.8 inch, 240 x 320 pixel TFT display with capacitive touch
controller is included with the BL5340 DVK for user interface
application features.

Refer to the `ER_TFTM028_4 datasheet`_ for a high level overview of the
display.

An ILI9341 TFT controller is incorporated in the TFT module and
acts as the main controller, controlled via SPI.

Refer to the `ILI9341 datasheet`_ for further details.

An FT6206 Capacitive Touch Controller, controlled via I2C is
also incorporated in the TFT module.

Refer to the `FT6206 datasheet`_ for further details.

Real-Time Clock
===============

A real-time clock is available for accurate time data availability.

Refer to the `Microchip MCP7940N datasheet`_ for further details.

DAC
===

A 10-bit Digital to Analog Converter is incorporated for generation of
variable voltages.

Refer to the `Microchip MCP4725 datasheet`_ for further details.

Security components
===================

- Implementation Defined Attribution Unit (`IDAU`_) on the application
  core. The IDAU is implemented with the System Protection Unit and is
  used to define secure and non-secure memory maps.  By default, all of
  the memory space (Flash, SRAM, and peripheral address space) is
  defined to be secure accessible only.
- Secure boot.

Programming and Debugging
*************************

The BL5340's application core supports the Armv8-M Security Extension.
Applications built for the ``bl5340_dvk/nrf5340/cpuapp`` board by default
boot in the Secure state.

The BL5340's network core does not support the Armv8-M Security
Extension. The IDAU may configure bus accesses by the network core to
have Secure attribute set; the latter allows to build and run Secure
only applications on the BL5340 module.

Building Secure/Non-Secure Zephyr applications with Arm |reg| TrustZone |reg|
=============================================================================

Applications on the BL5340 module may contain a Secure and a Non-Secure
firmware image for the application core. The Secure image can be built
using either Zephyr or `Trusted Firmware M`_ (TF-M). Non-Secure
firmware images are always built using Zephyr. The two alternatives are
described below.

.. note::

   By default the Secure image for BL5340's application core is
   built using TF-M.

Building the Secure firmware with TF-M
--------------------------------------

The process to build the Secure firmware image using TF-M and the
Non-Secure firmware image using Zephyr requires the following steps:

1. Build the Non-Secure Zephyr application
   for the application core using ``-DBOARD=bl5340_dvk/nrf5340/cpuapp/ns``.
   To invoke the building of TF-M the Zephyr build system requires the
   Kconfig option ``BUILD_WITH_TFM`` to be enabled, which is done by
   default when building Zephyr as a Non-Secure application.
   The Zephyr build system will perform the following steps automatically:

      * Build the Non-Secure firmware image as a regular Zephyr application
      * Build a TF-M (secure) firmware image
      * Merge the output image binaries together
      * Optionally build a bootloader image (MCUboot)

.. note::

   Depending on the TF-M configuration, an application DTS overlay may
   be required, to adjust the Non-Secure image Flash and SRAM starting
   address and sizes.

2. Build the application firmware for the network core using
   ``-DBOARD=bl5340_dvk/nrf5340/cpunet``.

Building the Secure firmware using Zephyr
-----------------------------------------

The process to build the Secure and the Non-Secure firmware images
using Zephyr requires the following steps:

1. Build the Secure Zephyr application for the application core
   using ``-DBOARD=bl5340_dvk/nrf5340/cpuapp`` and
   ``CONFIG_TRUSTED_EXECUTION_SECURE=y`` and ``CONFIG_BUILD_WITH_TFM=n``
   in the application project configuration file.
2. Build the Non-Secure Zephyr application for the application core
   using ``-DBOARD=bl5340_dvk/nrf5340/cpuapp/ns``.
3. Merge the two binaries together.
4. Build the application firmware for the network core using
   ``-DBOARD=bl5340_dvk/nrf5340/cpunet``.

When building a Secure/Non-Secure application for the BL5340's
application core, the Secure application will have to set the IDAU
(SPU) configuration to allow Non-Secure access to all CPU resources
utilized by the Non-Secure application firmware. SPU configuration
shall take place before jumping to the Non-Secure application.

Building a Secure only application
==================================

Build the Zephyr app in the usual way (see :ref:`build_an_application`
and :ref:`application_run`), using ``-DBOARD=bl5340_dvk/nrf5340/cpuapp`` for
the firmware running on the BL5340's application core, and using
``-DBOARD=bl5340_dvk/nrf5340/cpunet`` for the firmware running
on the BL5340's network core.

Flashing
========

Follow the instructions in the :ref:`nordic_segger` page to install
and configure all the necessary software. Further information can be
found in :ref:`nordic_segger_flashing`. Then you can build and flash
applications as usual (:ref:`build_an_application` and
:ref:`application_run` for more details).

.. warning::

   The BL5340 has a flash read-back protection feature. When flash
   read-back protection is active, you will need to recover the chip
   before reflashing. If you are flashing with
   :ref:`west <west-build-flash-debug>`, run this command for more
   details on the related ``--recover`` option:

   .. code-block:: console

      west flash -H -r nrfjprog --skip-rebuild

.. note::

   Flashing and debugging applications on the BL5340 DVK requires
   upgrading the nRF Command Line Tools to version 10.12.0 or newer.
   Further information on how to install the nRF Command Line Tools can
   be found in :ref:`nordic_segger_flashing`.

Here is an example for the :zephyr:code-sample:`hello_world` application running on the
BL5340's application core.

First, run your favorite terminal program to listen for output.

.. code-block:: console

   $ minicom -D <tty_device> -b 115200

Replace :code:`<tty_device>` with the port where the BL5340 DVK board
can be found. For example, under Linux, :code:`/dev/ttyACM0`.

Then build and flash the application in the usual way.

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: bl5340_dvk/nrf5340/cpuapp
   :goals: build flash

Debugging
=========

Refer to the :ref:`nordic_segger` page to learn about debugging
boards with a Segger IC.

Testing Bluetooth on the BL5340 DVK
***********************************
Many of the Bluetooth examples will work on the BL5340 DVK.
Try them out:

* :zephyr:code-sample:`ble_peripheral`
* :zephyr:code-sample:`bluetooth_eddystone`
* :zephyr:code-sample:`bluetooth_ibeacon`

References
**********

.. target-notes::

.. _IDAU:
   https://developer.arm.com/docs/100690/latest/attribution-units-sau-and-idau
.. _BL5340 homepage: https://www.ezurio.com/wireless-modules/bluetooth-modules/bluetooth-5-modules/bl5340-series-multi-core-bluetooth-52-802154-nfc-modules
.. _Nordic Semiconductor Infocenter: https://infocenter.nordicsemi.com
.. _TI TCA9538 datasheet: https://www.ti.com/lit/gpn/TCA9538
.. _Macronix MX25R6435FZNIL0 datasheet: https://www.macronix.com/Lists/Datasheet/Attachments/8868/MX25R6435F,%20Wide%20Range,%2064Mb,%20v1.6.pdf
.. _Giantec GT24C256C-2GLI-TR datasheet: https://www.giantec-semi.com/juchen1123/uploads/pdf/GT24C256C_DS_Cu.pdf
.. _Bosch BME680 datasheet: https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme680-ds001.pdf
.. _ST Microelectronics LIS3DH datasheet: https://www.st.com/resource/en/datasheet/lis3dh.pdf
.. _Microchip ENC424J600 datasheet: https://ww1.microchip.com/downloads/en/DeviceDoc/39935c.pdf
.. _ER_TFTM028_4 datasheet: https://www.buydisplay.com/download/manual/ER-TFTM028-4_Datasheet.pdf
.. _ILI9341 datasheet: https://www.buydisplay.com/download/ic/ILI9341.pdf
.. _FT6206 datasheet: https://www.buydisplay.com/download/ic/FT6206.pdf
.. _Microchip MCP7940N datasheet: https://ww1.microchip.com/downloads/en/DeviceDoc/20005010H.pdf
.. _Microchip MCP4725 datasheet: https://ww1.microchip.com/downloads/en/DeviceDoc/22039d.pdf
.. _Trusted Firmware M: https://www.trustedfirmware.org/projects/tf-m/
