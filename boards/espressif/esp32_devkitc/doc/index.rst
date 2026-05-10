.. zephyr:board:: esp32_devkitc

Overview
********

ESP32 is a series of low cost, low power system on a chip microcontrollers
with integrated Wi-Fi & dual-mode Bluetooth. The ESP32 series employs a
Tensilica Xtensa LX6 microprocessor in both dual-core and single-core
variations. ESP32 is created and developed by Espressif Systems, a
Shanghai-based Chinese company, and is manufactured by TSMC using their 40nm
process. For more information, check `ESP32-DevKitC`_.

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

On the ESP32-DevKitC board, the JTAG pins are not run to a
standard connector (e.g. ARM 20-pin) and need to be manually connected
to the external programmer (e.g. a Flyswatter2):

+------------+-----------+
| ESP32 pin  | JTAG pin  |
+============+===========+
| 3V3        | VTRef     |
+------------+-----------+
| EN         | nTRST     |
+------------+-----------+
| IO14       | TMS       |
+------------+-----------+
| IO12       | TDI       |
+------------+-----------+
| GND        | GND       |
+------------+-----------+
| IO13       | TCK       |
+------------+-----------+
| IO15       | TDO       |
+------------+-----------+

References
**********

.. target-notes::

.. _`ESP32-DevKitC`: https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32/esp32-devkitc/index.html
