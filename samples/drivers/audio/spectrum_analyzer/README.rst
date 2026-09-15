.. zephyr:code-sample:: audio-spectrum-analyzer
   :name: Audio spectrum analyzer
   :relevant-api: i2s_interface audio_codec_interface display_interface

   Show a live frequency spectrum of captured audio on a display.

Overview
********

This sample captures audio over :ref:`I2S <i2s_api>`, transforms it with the
CMSIS-DSP library and draws the result as a bar per frequency band on the
:ref:`display <display_api>` the board selects with the ``zephyr,display``
chosen node. Bands are spaced logarithmically, the way the ear hears pitch, and
each carries a peak marker that falls back slowly.

Every captured block is windowed, transformed with a real FFT and folded into
band levels expressed in dB relative to a full-scale sine. Capture runs in its
own thread and hands the renderer only the newest frame, so a display that
cannot keep up drops frames rather than overrunning the audio interface.

Requirements
************

A board with an audio input on an I2S controller labelled ``i2s_rx`` and a
monochrome display, whose binding carries ``width`` and ``height`` properties,
selected as ``zephyr,display``. When the board also has an audio codec in front
of the microphone, labelling it ``audio_codec`` makes the sample configure its
capture path and input gain.

The sample has been tested on :zephyr:board:`az3166_iotdevkit`, whose
microphone reaches the SoC through a NAU88C10 codec and the I2S2 extension
block, and which carries a 128x64 SSD1306 OLED.

Building and Running
********************

The code can be found in :zephyr_file:`samples/drivers/audio/spectrum_analyzer`.

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/audio/spectrum_analyzer
   :board: az3166_iotdevkit
   :goals: build flash
   :compact:

Make a sound near the microphone and the bars follow it. The sample prints a
single line on the console at startup and then leaves the display alone:

.. code-block:: console

   *** Booting Zephyr OS build v4.4.0 ***
   [00:00:00.066,000] <inf> spectrum: 20 bands of 16000 Hz audio, 512 point transform

The sample rate, the number of bands, the transform length and the codec input
gain are all :file:`Kconfig` options. A longer transform resolves low
frequencies better and refreshes the display less often: at the default 16 kHz
and 512 points, a frame covers 32 ms and the display refreshes about 31 times a
second.
