.. zephyr:board:: esp32c5_devkitc

Overview
********

ESP32-C5-DevKitC-1 is an entry-level development board based on ESP32-C5-WROOM-1,
a general-purpose module with a 8 MB SPI flash and 4 MB PSRAM. This board integrates complete
dual-band Wi-Fi 6, Bluetooth LE, Zigbee, and Thread functions.
For more information, check `ESP32-C5-DevKitC-1`_.

Hardware
********

Most of the I/O pins are broken out to the pin headers on both sides for easy interfacing.
Developers can either connect peripherals with jumper wires or mount ESP32-C5-DevKitC-1 on
a breadboard.

.. zephyr:board-soc-fragment:: soc-features

Supported Features
==================

.. zephyr:board-supported-hw::

System Requirements
*******************

.. zephyr:board-soc-fragment:: system-requirements

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

.. zephyr:board-soc-fragment:: building-flashing

.. zephyr:board-soc-fragment:: board-variants

Debugging
=========

.. zephyr:board-soc-fragment:: openocd-debugging

References
**********

.. target-notes::

.. _`ESP32-C5-DevKitC-1`: https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c5/esp32-c5-devkitc-1/user_guide.html
