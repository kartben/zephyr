.. zephyr:board:: m5stack_papercolor

Overview
********

The M5Stack PaperColor is an ESP32-S3 development board built around a 4"
6-color e-paper display. It pairs the ESP32-S3R8 SoC with M5Stack's M5PM1
power management IC to provide deep-sleep-friendly battery operation.

Hardware
********

- ESP32-S3R8 (dual-core Xtensa LX7 @ 240 MHz, 8 MB embedded PSRAM, Wi-Fi)
- 16 MB external SPI flash (W25Q128JV)
- 4" 6-color e-paper display, 400x600 (E-Ink ED2208-DOA / EL040EF1)
- M5PM1 PMIC (custom PY32-based controller, I2C 0x6E)
- RX8130CE RTC with battery backup (I2C 0x32)
- SHT40 temperature/humidity sensor (I2C 0x44)
- ES8311 audio codec + ES7210 mic ADC + AW8737A speaker amp (I2S, MEMS mic)
- microSD card slot
- 2x WS2812 RGB LEDs
- IR transmitter
- 3x user buttons + 1 power button
- USB Type-C (CDC console)
- 1250 mAh battery
- HY2.0-4P Grove (Port.A)

.. include:: ../../../espressif/common/soc-esp32s3-features.rst
   :start-after: espressif-soc-esp32s3-features

Supported Features
==================

.. zephyr:board-supported-hw::

The current board support exposes the ESP32-S3, USB CDC console, the
RX8130CE RTC, the SHT40 sensor, the WS2812 RGB chain, the microSD card
slot, the three user buttons, and the M5PM1 PMIC's three switchable rails
(``DCDC 5V``, ``LDO 3.3V``, ``BOOST 5V``) through Zephyr's regulator API.

The following peripherals are present on the board but **not yet
supported** in Zephyr:

- 4" e-paper panel (no in-tree driver for the EL040EF1 controller).
- ES8311 audio codec, ES7210 mic ADC and AW8737A speaker amplifier (I2S).
- IR transmitter on GPIO48 (RMT).
- M5PM1 sub-functions other than the regulator: GPIO expander (5 pins),
  ADC, PWM, watchdog, button events, NeoPixel driver, RTC RAM, and the
  shutdown / reboot / download-mode commands.

System requirements
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

.. _`M5Stack PaperColor Documentation`: https://docs.m5stack.com/en/core/PaperColor
.. _`M5PM1 reference driver`: https://github.com/m5stack/M5PM1
