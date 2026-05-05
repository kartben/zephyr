.. _led_strip_api:

LED Strip
#########

Overview
********

The LED strip driver API provides a common interface for individually
addressable LED strips — linear arrays of LEDs where each LED's color can be
set independently. Typical hardware uses protocols such as WS2812/WS2812B,
APA102/SK9822, LPD880x, or similar.

The API represents each LED as an :c:struct:`led_rgb` structure containing
red, green, and blue channel values (0–255). A single call to
:c:func:`led_strip_update_rgb` writes a complete array of pixel values to the
strip. Some controllers also support a white (W) channel; the API allows
fetching per-device channel counts with :c:func:`led_strip_length`.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_LED_STRIP`

API Reference
*************

.. doxygengroup:: led_strip_interface
