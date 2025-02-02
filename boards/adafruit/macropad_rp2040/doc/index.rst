.. zephyr:board:: adafruit_macropad_rp2040

Adafruit MacroPad RP2040
########################

Overview
********

The Adafruit MacroPad RP2040 is a 3x4 mechanical keyboard development board featuring
the Raspberry Pi RP2040 microcontroller. It includes 12 mechanical key switches with
individual RGB NeoPixels, a rotary encoder with push button, a 128x64 OLED display,
and a small speaker for audio feedback.

Hardware
********

- Dual core Cortex-M0+ at up to 133MHz
- 264KB of SRAM
- 8MB of QSPI flash memory
- 12 mechanical key switches (Cherry MX compatible)
- 12 RGB NeoPixel LEDs (one per key)
- 128x64 monochrome OLED display (SH1106)
- Rotary encoder with push button
- Small speaker with Class D amplifier
- STEMMA QT / Qwiic I2C connector
- USB Type-C connector
- Reset button
- 4x M3 mounting holes

Supported Features
******************

The ``adafruit_macropad_rp2040`` board target supports the following hardware features:

TBC

Programming and Debugging
*************************

The easiest way to program and debug the MacroPad RP2040 is to use UF2 files.
To enter the UF2 bootloader, hold down the rotary encoder button and, while continuing to hold it, press
and release the reset button. A "RPI-RP2" drive should appear.

Building and Flashing
*********************

Here is an example for building and flashing the :zephyr:code-sample:`blinky` sample application.

.. zephyr-app-commands::
   :zephyr-app: samples/basic/blinky
   :board: adafruit_macropad_rp2040
   :goals: build flash
   :flash-args: --runner uf2

References
**********

- `Adafruit MacroPad RP2040 Product Page <https://www.adafruit.com/product/5128>`_
- `Adafruit MacroPad RP2040 Learn Guide <https://learn.adafruit.com/adafruit-macropad-rp2040>`_
