.. zephyr:board:: xteink_x4

Overview
********

The Xteink X4 is a compact e-paper reader based on the Espressif ESP32-C3 (RISC-V),
with a 4.26" 800×480 monochrome panel driven by an SSD1677 controller (Good Display
GDEQ0426T82 class), microSD on a shared SPI bus, and a mix of GPIO and ADC ladder
inputs for keys.

Hardware
********

- **MCU**: ESP32-C3 (160 MHz RISC-V, 400 KB SRAM, no PSRAM)
- **Flash**: 16 MB (on production units; adjust devicetree if your unit differs)
- **Display**: SSD1677 over SPI (800×480), BUSY on GPIO6
- **Connectivity**: USB serial/JTAG, 2.4 GHz Wi-Fi and Bluetooth LE (ESP32-C3 radio)

Pinout and peripherals (ESP32-C3 GPIO)
========================================

SPI2 is shared between the e-paper panel and the microSD socket (separate chip selects).

+------------------+------------------+------------------------------------------+
| Function         | GPIO             | Notes                                    |
+==================+==================+==========================================+
| EPD SCK          | 8                | SPI clock                                |
+------------------+------------------+------------------------------------------+
| EPD MOSI         | 10               | SPI MOSI to panel and SD                 |
+------------------+------------------+------------------------------------------+
| SD MISO          | 7                | SPI MISO from microSD only               |
+------------------+------------------+------------------------------------------+
| EPD CS           | 21               | Panel chip select (GPIO CS)            |
+------------------+------------------+------------------------------------------+
| SD CS            | 12               | microSD chip select (not in Zephyr DTS)  |
+------------------+------------------+------------------------------------------+
| EPD DC           | 4                | Data/command                             |
+------------------+------------------+------------------------------------------+
| EPD RST          | 5                | Reset (active low)                       |
+------------------+------------------+------------------------------------------+
| EPD BUSY         | 6                | Busy (active high)                       |
+------------------+------------------+------------------------------------------+
| Power key        | 3                | Direct GPIO (active low)                 |
+------------------+------------------+------------------------------------------+
| Nav keys (ADC)   | 1, 2             | Resistor ladder; read via ADC1_CH0/CH1   |
+------------------+------------------+------------------------------------------+
| Battery sense    | 0                | Divided battery voltage (ADC1 channel 0) |
+------------------+------------------+------------------------------------------+
| USB charging     | 20               | Charging detect (GPIO input)             |
+------------------+------------------+------------------------------------------+

On ESP32-C3, GPIO1 and GPIO2 are typically ADC1 channels 1 and 2, so battery (GPIO0 /
channel 0) and the two ladder inputs can be sampled independently. Thresholds for
each navigation key still need application-level calibration (see the Adafruit
CircuitPython helper or Papyrix for reference values).

References
==========

- `CircuitPython on the Xteink X4 — Pinouts`_
- `Papyrix reader (SSD1677 notes and firmware)`_

.. _CircuitPython on the Xteink X4 — Pinouts:
   https://learn.adafruit.com/circuitpython-on-the-xteink-x4-ereader/pinouts

.. _Papyrix reader (SSD1677 notes and firmware):
   https://github.com/bigbag/papyrix-reader

Supported features
******************

The Zephyr board port enables:

* USB CDC console (``usb_serial``)
* SPI2 with GPIO chip select for the panel
* SSD1677 / SSD16XX display driver via MIPI DBI SPI
* ADC, GPIO, Wi-Fi, Bluetooth HCI, watchdog

UART0 is disabled in devicetree so GPIO20/GPIO21 are available for other use (e.g.
charging detect on GPIO20 as on CircuitPython’s ``board.D20``).

Building and flashing
*********************

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/display/lvgl
   :board: xteink_x4
   :goals: build flash
   :compact:

Use the ESP32-C3 USB port for flashing and console. For a minimal display smoke test,
any sample that uses ``zephyr,display`` and the monochrome display API can be adapted.

microSD (SPI)
=============

The card shares SPI2 with MOSI/SCK and uses GPIO7 for MISO and GPIO12 for CS. A Zephyr
MMC/SPI disk driver is not wired in this board file yet; add a SPI SD child node under
``&spi2`` when you enable the relevant subsystem.

Display driver caveat
=====================

The SSD1677 differs from smaller SSD16xx parts (command set, RAM addressing). This tree
adds a ``solomon,ssd1677`` compatible and driver quirks for gate count and 16-bit
coordinates. Full visual correctness may still need panel-specific LUT or orientation
tuning; see the Papyrix SSD1677 documentation for low-level sequences.
