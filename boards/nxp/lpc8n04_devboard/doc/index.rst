.. zephyr:board:: lpc8n04_devboard

Overview
********

The LPC8N04 Development Board (OM40002) from NXP is an evaluation board for the LPC8N04, an
Arm Cortex-M0+ MCU with an integrated NFC Forum Type 2 tag interface. The LPC8N04 can run from
a battery or be powered by the field of an NFC reader such as a smartphone.

The board has two parts: an LPC11U35 CMSIS-DAP debug probe with a USB connector on the left,
and the LPC8N04 target with its antenna, coin cell holder and a 5x7 LED array on the right.

Hardware
********

- LPC8N04 Arm Cortex-M0+ MCU, up to 8 MHz from the internal 8 MHz oscillator
- 32 KB flash, 8 KB SRAM, 4 KB EEPROM
- NFC/RFID ISO 14443 type A interface with an on-board antenna, 512-byte buffer shared with the
  reader
- Temperature sensor, I2C, SPI, 16-bit and 32-bit timers, RTC wake-up timer
- 12 GPIO pins, no UART
- 5x7 LED array, speaker, user button (S2), reset button (S3), target power switch (S1)
- On-board CMSIS-DAP debug probe (LPC11U35), external SWD connector (J1)

The system clock is set by the ``clock-frequency`` of the ``cpu0`` node: 8 MHz divided by a
power of two. The board runs at 8 MHz. Without a battery the LPC8N04 is limited to 2 MHz, so a
board powered only by the NFC field must set ``clock-frequency`` to 2 MHz or less.

.. zephyr:board-supported-hw::

Connections and IOs
===================

+---------+--------------------------------------------+
| Pin     | Usage on the board                         |
+=========+============================================+
| PIO0_0  | User button S2 (to ground through 1 kΩ),   |
|         | LED row 5                                  |
+---------+--------------------------------------------+
| PIO0_1  | LED row 4                                  |
+---------+--------------------------------------------+
| PIO0_2  | LED row 0 (``led0``)                       |
+---------+--------------------------------------------+
| PIO0_3  | LED row 6 or speaker, selected by P9       |
+---------+--------------------------------------------+
| PIO0_4  | LED column 2                               |
+---------+--------------------------------------------+
| PIO0_5  | LED column 1                               |
+---------+--------------------------------------------+
| PIO0_6  | LED column 0, held low for ``led0``        |
+---------+--------------------------------------------+
| PIO0_7  | LED row 1                                  |
+---------+--------------------------------------------+
| PIO0_8  | LED row 2                                  |
+---------+--------------------------------------------+
| PIO0_9  | LED row 3                                  |
+---------+--------------------------------------------+
| PIO0_10 | SWCLK or LED column 3, selected by P8      |
+---------+--------------------------------------------+
| PIO0_11 | SWDIO or LED column 4, selected by P7      |
+---------+--------------------------------------------+

An LED of the array lights when its row is driven high and its column low. ``led0`` is the LED
at row 0, column 0: the devicetree drives column 0 (PIO0_6) low with a GPIO hog and toggles
row 0 (PIO0_2).

Console
=======

The LPC8N04 has no UART. The board uses SEGGER RTT for the console, read through the on-board
debug probe:

.. code-block:: console

   west rtt

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

Set jumpers P7 and P8 to the SWD position and switch S1 on to power the target. The board is
flashed and debugged with pyOCD through the on-board CMSIS-DAP probe. pyOCD takes the LPC8N04
target from NXP's CMSIS pack, which has to be installed once:

.. code-block:: console

   pyocd pack install lpc8n04

Flashing
========

Here is an example for the :zephyr:code-sample:`blinky` application.

.. zephyr-app-commands::
   :zephyr-app: samples/basic/blinky
   :board: lpc8n04_devboard
   :goals: flash

Debugging
=========

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: lpc8n04_devboard
   :goals: debug

References
**********

- `LPC8N04 Development Board website`_
- `LPC8N04 data sheet`_
- `LPC8N04 product page`_ (user manual UM11074 and board user manual UM11082)

.. include:: ../../common/board-footer.rst.inc

.. _LPC8N04 Development Board website:
   https://www.nxp.com/design/design-center/development-boards-and-designs/lpc8n04-development-board-for-lpc8n04-mcu:OM40002

.. _LPC8N04 data sheet:
   https://www.nxp.com/docs/en/data-sheet/LPC8N04.pdf

.. _LPC8N04 product page:
   https://www.nxp.com/products/LPC8N04
