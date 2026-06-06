.. zephyr:board:: weact_ch32v006

Overview
********

The `WeActStudio`_ CH32V00x Core Board is a compact development board built around the RISC-V based
CH32V006F8U6 SOC and the following devices:

* CLOCK
* :abbr:`GPIO (General Purpose Input Output)`
* :abbr:`NVIC (Nested Vectored Interrupt Controller)`
* :abbr:`UART (Universal Asynchronous Receiver-Transmitter)`
* :abbr:`IWDG (Independent Watchdog)`

The board is equipped with a single blue user LED and a reset button, and is
powered through a USB Type-C connector. The `WCH webpage on CH32V006`_ contains
the processor's information and the datasheet. The `WeActStudio webpage on the
CH32V00x Core Board`_ contains the board's schematic.

Hardware
********

The QingKe V2C 32-bit RISC-V processor of the WeActStudio CH32V006 Core Board is clocked by the
internal 24 MHz oscillator (the board has no external crystal) and runs at up to 48 MHz. The
CH32V006 SoC features 8 KB of SRAM, 62 KB of flash, two USARTs, four GPIO ports, one SPI, one I2C,
an ADC, a DMA controller, several timers, and an independent watchdog.

Supported Features
==================

.. zephyr:board-supported-hw::

Connections and IOs
===================

LED
---

* LED0 = Blue user LED (PC4)

UART
----

* The console is exposed on USART1 (TX = PD5, RX = PD6) at 115200 baud.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

Applications for the ``weact_ch32v006`` board can be built and flashed in the usual way (see
:ref:`build_an_application` and :ref:`application_run` for more details); however, an external
programmer (such as a WCH-LinkE) is required since the board does not have any built-in debug
support.

Connect the programmer to the following pins on the board's DEBUG header:

* VCC = VCC (do not power the board from the USB port at the same time)
* GND = GND
* SWIO = PD1
* NRST = PD7

Flashing
========

You can use minichlink_ to flash the board. Once ``minichlink`` has been set up, build and flash
applications as usual (see :ref:`build_an_application` and :ref:`application_run` for more details).

Here is an example for the :zephyr:code-sample:`blinky` application.

.. zephyr-app-commands::
   :zephyr-app: samples/basic/blinky
   :board: weact_ch32v006
   :goals: build flash

Debugging
=========

This board can be debugged via OpenOCD or ``minichlink``.

References
**********

.. target-notes::

.. _WeActStudio: https://github.com/WeActStudio
.. _WCH webpage on CH32V006: https://www.wch-ic.com/downloads/CH32V006DS0_PDF.html
.. _WeActStudio webpage on the CH32V00x Core Board: https://github.com/WeActStudio/WeActStudio.CH32V00xCoreBoard
.. _minichlink: https://github.com/cnlohr/ch32fun/tree/master/minichlink
