.. _pov_led_strip_sample:

Persistence-of-Vision LED Strip Sample
======================================

Overview
--------

This sample demonstrates the :ref:`LED strip POV display driver
<display_api>` by streaming a simple animated pattern to a single LED strip
while a tachometer controls the refresh rate. The pattern buffer is updated
with the Display API so that the driver can translate it into column updates
on the strip.

Requirements
------------

* A board with SPI support and available GPIOs (tested on ``nrf52dk_nrf52832``)
* A WS2812-compatible LED strip wired to the Arduino MOSI pin
* A tachometer sensor connected to the Arduino D2 pin providing RPM pulses

Building and Running
--------------------

To build for ``nrf52dk_nrf52832``::

   west build -b nrf52dk_nrf52832 samples/display/pov_led_strip

Once running, the console prints the generated frame number while the LED
strip should display a flowing color pattern.
