.. zephyr:board:: xteink_x4

Overview
********

The Xteink X4 is an affordable e-paper reader device built around the ESP32-C3
microcontroller. It features a 4.26-inch e-ink display (800x480), microSD card
slot, 7 navigation buttons, battery monitoring, Wi-Fi, and Bluetooth LE.

.. figure:: img/xteink_x4.webp
   :align: center
   :alt: Xteink X4

Hardware
********

- **Processor**: ESP32-C3 (RISC-V RV32IMC) at 160 MHz
- **SRAM**: 400 KB (380 KB usable)
- **Flash**: 16 MB
- **Display**: 4.26" e-ink (800x480), SSD1677 controller via SPI
- **Storage**: microSD card via SPI
- **Connectivity**: Wi-Fi 802.11 b/g/n, Bluetooth LE 5.0
- **Battery**: LiPo with ADC monitoring and USB charge detection
- **Buttons**: 7 (1 GPIO power button + 6 via resistor ladder on 2 ADC channels)

Pin Mapping
===========

The following table shows the pin assignment for the Xteink X4:

.. list-table::
   :header-rows: 1

   * - GPIO
     - Function
     - Direction
   * - GPIO0
     - Battery ADC
     - Input
   * - GPIO1
     - Button ADC channel 1 (Back/Confirm/Left/Right)
     - Input
   * - GPIO2
     - Button ADC channel 2 (Up/Down)
     - Input
   * - GPIO3
     - Power button (active LOW, pullup)
     - Input
   * - GPIO4
     - Display DC (data/command)
     - Output
   * - GPIO5
     - Display RST (active LOW)
     - Output
   * - GPIO6
     - Display BUSY (LOW = busy)
     - Input
   * - GPIO7
     - SD card MISO
     - Input
   * - GPIO8
     - SPI SCLK (shared display + SD)
     - Output
   * - GPIO10
     - SPI MOSI (shared display + SD)
     - Output
   * - GPIO12
     - SD card CS
     - Output
   * - GPIO20
     - Battery charge detect
     - Input
   * - GPIO21
     - Display CS (active LOW)
     - Output

Supported Features
==================

.. zephyr:board-supported-hw::

System Requirements
*******************

.. include:: ../../../espressif/common/system-requirements.rst
   :start-after: espressif-system-requirements

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

.. include:: ../../../espressif/common/building-flashing.rst
   :start-after: espressif-building-flashing

.. include:: ../../../espressif/common/board-variants.rst
   :start-after: espressif-board-variants

Debugging
=========

.. include:: ../../../espressif/common/openocd-debugging.rst
   :start-after: espressif-openocd-debugging

References
**********

- `Adafruit Xteink X4 Pinouts <https://learn.adafruit.com/circuitpython-on-the-xteink-x4-ereader/pinouts>`_
- `Papyrix Reader Firmware <https://github.com/bigbag/papyrix-reader>`_
- `ESP32-C3 Datasheet <https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf>`_

.. target-notes::
