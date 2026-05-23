.. zephyr:board:: t_deck

Overview
********

The LilyGO T-Deck is a handheld ESP32-S3 device with a landscape display,
capacitive touch panel, external keyboard controller, trackball, and wireless
connectivity. The T-Deck Plus adds a battery fuel gauge and an optional GNSS
receiver.

Board revisions
===============

Use the ``plus`` board revision for the T-Deck Plus hardware::

   west build -b t_deck@plus/esp32s3/procpu ...

The standard T-Deck (without ``@plus``) omits the composite fuel gauge and GNSS
nodes. Battery voltage measurement via ``vbatt`` is available on all boards.

Hardware
********

- ESP32-S3-WROOM-N16R8 module

  - Dual core Xtensa LX7 up to 240 MHz
  - 16 MB flash
  - 8 MB PSRAM
  - Wi-Fi 802.11 b/g/n
  - Bluetooth LE 5.0

- 2.8 inch 320x240 SPI TFT display
- Goodix GT911 capacitive touch controller
- External I2C keyboard controller with a 35-key keyboard
- Trackball with four direction inputs and a center press button
- Battery voltage measurement on IO4 through a 100k/100k resistor divider
- USB-C for power, flashing, logging, and debugging through the built-in USB JTAG
- Trackball portable form factor

T-Deck Plus (``@plus`` revision)
==================================

- Composite fuel gauge based on the battery voltage sensor
- Optional GNSS module on UART0 (GPIO43 TX, GPIO44 RX), sharing the Grove connector pins

GNSS
====

T-Deck Plus boards with GPS populate a module on UART0 at GPIO43 (TX) and GPIO44 (RX).
These pins are normally used by the Grove port, so the Grove connector is unavailable when
GPS is fitted.

LilyGO documents the current module as a u-blox MIA-M10Q (38400 baud). Older or
alternate SKUs may use a Quectel L76K (9600 baud). The devicetree defaults to 38400 baud;
for an L76K module, set ``current-speed`` on ``&uart0`` to ``9600`` in the application
devicetree overlay.

The current Zephyr support focuses on the main ESP32-S3 application processor,
USB serial console, the integrated display, touch controller, keyboard
controller, trackball buttons, and battery voltage measurement. On the ``plus``
revision, the composite fuel gauge and GNSS are also supported. Audio, LoRa, SD
card, and other board-specific peripherals are not fully described yet.

The keyboard is driven by a separate ESP32-C3 running the LilyGO
``Keyboard_ESP32C3`` firmware. If keys are not detected, verify that firmware
is installed on the keyboard MCU. Peripheral power is enabled by the
``peripheral_pwr`` GPIO power domain (GPIO10), which is turned on before
dependent devices (I2C, SPI, trackball, I2S) are initialized.

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

The board enters the boot ROM download mode when the center trackball button is
held while resetting or powering the device.

.. include:: ../../../espressif/common/board-variants.rst
   :start-after: espressif-board-variants

Debugging
=========

.. include:: ../../../espressif/common/openocd-debugging.rst
   :start-after: espressif-openocd-debugging

References
**********

.. target-notes::

.. _`LilyGO T-Deck repository`: https://github.com/Xinyuan-LilyGO/T-Deck
.. _`LilyGO T-Deck README`: https://github.com/Xinyuan-LilyGO/T-Deck#readme
