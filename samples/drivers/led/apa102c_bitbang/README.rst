.. zephyr:code-sample:: apa102c_bitbang
   :name: APA102C LED Bitbang Control
   :relevant-api: gpio_interface

   Control APA102C LEDs using GPIO bit-banging.

Overview
********

This sample demonstrates how to control APA102C (DotStar) LEDs using GPIO
bit-banging. The sample cycles through different RGB color values to display
on the LED strip.

The APA102C is a chainable RGB LED that uses a simple two-wire (data and clock)
protocol. This sample implements the protocol using GPIO pins directly, without
requiring a dedicated LED controller hardware.

Requirements
************

* A board with GPIO support
* APA102C LED strip or individual LEDs
* Logic level shifter or appropriate pull-up resistors (APA102C requires 5V signals)

.. warning::

   The APA102C LEDs are very bright even at low settings. Protect your eyes
   and do not look directly into the LEDs. This sample uses reduced brightness
   and RGB values for safety.

Hardware Setup
**************

The APA102C requires 5V data and clock signals, so a logic level shifter
(preferred) or pull-up resistors are needed. Make sure the pins are 5V tolerant
if using pull-up resistors.

Connect the APA102C as follows:

- Data pin: GPIO 16 (configurable)
- Clock pin: GPIO 19 (configurable)
- VCC: 5V
- GND: Ground

Building and Running
********************

This sample can be built for any board with GPIO support. Configure the GPIO
pins in the source code to match your hardware setup.

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/led/apa102c_bitbang
   :board: <your_board>
   :goals: build flash
   :compact:

Sample Output
=============

The sample will continuously cycle through different colors on the LED strip.
Console output shows:

.. code-block:: console

   Cycling through RGB colors on APA102C LED
