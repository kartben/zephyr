.. zephyr:code-sample:: libdither-sample
   :name: Image Dithering (libdither)
   :relevant-api: libdither_interface display_interface

   Convert a grayscale test pattern to 1-bit monochrome using libdither and display the result.

Overview
********

This sample demonstrates the :ref:`libdither_interface` image dithering library.
It generates a diagonal grayscale gradient, converts it to 1-bit monochrome using
Floyd-Steinberg error diffusion dithering, and writes the result to a monochrome display.

The library supports three algorithms:

- **Threshold**: Simple cutoff at intensity 128. Fast but produces harsh edges.
- **Ordered**: 4x4 Bayer matrix dithering. Good balance of speed and quality; no extra
  memory required.
- **Floyd-Steinberg**: Error diffusion dithering. Highest quality; modifies the input
  buffer in-place.

Building and Running
********************

Build and run on native_sim using a dummy 128x64 monochrome display:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/display/libdither
   :board: native_sim/native/64
   :goals: build run
   :gen-args: -DDTC_OVERLAY_FILE=dummy_dc.overlay -DCONFIG_SDL_DISPLAY=n
   :compact:

Build for an nRF52840 DK with an SSD1306 128x64 OLED shield:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/display/libdither
   :board: nrf52840dk/nrf52840
   :goals: build flash
   :gen-args: -DSHIELD=ssd1306_128x64
   :compact:

Expected output on the console::

   [00:00:00.000,000] <inf> libdither_sample: Display: DISPLAY_SSD1306 128x64 ...
   [00:00:00.010,000] <inf> libdither_sample: Display sample done
