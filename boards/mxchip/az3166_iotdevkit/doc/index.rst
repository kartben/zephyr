.. zephyr:board:: az3166_iotdevkit

Overview
********

The AZ3166 IoT DevKit from MXChip is a development board designed for IoT (Internet of Things)
projects. It's an all-in-one board powered by an Arm Cortex-M4 processor. On-board peripherals
include an OLED screen, headphone output, microphone and abundant sensors like humidity &
temperature, pressure, motion (accelerometer & gyroscope) and magnetometer.

More information about the board can be found at the `MXChip AZ3166 website`_.

Hardware
********

The MXChip AZ3166 IoT DevKit has the following physical features:

* STM32F412 Arm Cortex M4F processor at 96 MHz
* Working voltage: 3.3v or USB power supply
* Supports 3.3V DC-DC, maximum current 1.5A
* OLED display, 128x64 pixels
* 2 programmable buttons
* 1 RGB LED
* 3 LED for status indicators ("Wi-Fi", "Azure", "User")
* Security encryption chip
* Infrared emitter for IR remote control or interaction
* Motion sensor (LSM6DSL)
* Magnetometer sensor (LIS2MDL)
* Atmospheric pressure sensor (LPS22HB)
* Temperature and humidity sensor (HTS221)
* Mono audio codec (NAU88C10) driving the headphone jack and the onboard microphone
* EMW3166 Wi-Fi module with 256K SRAM，1M+2M Byte SPI Flash


Supported Features
==================

.. zephyr:board-supported-hw::

.. note::

   The EMW3166 Wi-Fi module is currently not supported.

Audio
=====

The onboard microphone is an analog microphone wired differentially to the MIC+/MIC- inputs of
a NAU88C10 mono codec, which also drives the headphone jack. The codec is controlled over I2C
and exchanges PCM audio with the SoC over I2S2, the SoC supplying the master clock, the bit
clock and the frame clock. See :ref:`audio_codec_api` and :ref:`i2s_api`.

The codec DAC input hangs off the data line of the I2S2 block, while its ADC output reaches
the SoC on PB14, the data line of the I2S2ext extension block. Capture therefore runs on the
extension block, which the main block clocks, so playback and capture can run at the same
time but always share a single configuration.

The codec is mono and only uses the left slot of the frame: captured frames carry the
microphone in the left channel, and the DAC plays back the left channel.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

Flashing
========

Build and flash applications as usual (see :ref:`build_an_application` and
:ref:`application_run` for more details).

Here is an example for the :zephyr:code-sample:`hello_world` application.

First, run your favorite terminal program to listen for output.

.. code-block:: console

   $ minicom -D <tty_device> -b 115200

Replace :code:`<tty_device>` with the port where the micro:bit board
can be found. For example, under Linux, :code:`/dev/ttyACM0`.

Then build and flash the application in the usual way.

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: az3166_iotdevkit
   :goals: build flash


References
**********

.. target-notes::

.. _MXChip AZ3166 website: https://www.mxchip.com/en/az3166
