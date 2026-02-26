.. zephyr:board:: uedx80480070e_wb_a

Overview
********

The VIEWE UEDX80480070E-WB-A is a 7" 800x480 IPS TFT display module with ESP32-S3 MCU and
capacitive touch. It features Wi-Fi and Bluetooth Low Energy connectivity, making it suitable for
IoT applications with graphical user interfaces.

Key features include:

- ESP32-S3 dual-core Xtensa LX7 @ 240MHz
- 16MB Flash, 8MB PSRAM (Octal SPI)
- 7" IPS TFT display (800x480, EK9716BD3+EK73002AB2 RGB driver)
- Capacitive touch panel (GT911 controller)
- SD card slot (SPI interface)
- USB-C for power and programming (CH340C)
- Boot and Reset buttons

Hardware
********

.. include:: ../../../espressif/common/soc-esp32s3-features.rst
   :start-after: espressif-soc-esp32s3-features

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

.. target-notes::
