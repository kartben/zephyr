.. zephyr:board:: esp32s2_devkitc

Overview
********

ESP32-S2-DevKitC is an entry-level development board. This board integrates complete Wi-Fi functions.
Most of the I/O pins are broken out to the pin headers on both sides for easy interfacing.
Developers can either connect peripherals with jumper wires or mount ESP32-S2-DevKitC on a breadboard.
For more information, check `ESP32-S2-DevKitC`_.

Hardware
********

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

The following table shows the pin mapping between ESP32-S2 board and JTAG interface.

+---------------+-----------+
| ESP32 pin     | JTAG pin  |
+===============+===========+
| MTDO / GPIO40 | TDO       |
+---------------+-----------+
| MTDI / GPIO41 | TDI       |
+---------------+-----------+
| MTCK / GPIO39 | TCK       |
+---------------+-----------+
| MTMS / GPIO42 | TMS       |
+---------------+-----------+

References
**********

.. target-notes::

.. _`ESP32-S2-DevKitC`: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s2/hw-reference/esp32s2/user-guide-saola-1-v1.2.html
