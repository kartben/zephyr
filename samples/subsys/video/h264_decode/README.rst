.. zephyr:code-sample:: h264_decode
   :name: H.264 software decode
   :relevant-api: video_interface

   Decode an H.264 bitstream with edge264 and display the frames.

Overview
********

This sample feeds a small Annex-B H.264 clip to the :dtcompatible:`zephyr,video-edge264`
memory-to-memory decoder and, when a display is present, shows the reconstructed
RGB565 frames.

The clip is a 160x120 constrained-baseline intra frame generated with x264.
On :zephyr:board:`stm32n6570_dk` the decoder heap and video buffers are placed
in PSRAM. Cortex-M55 has Helium rather than NEON, so the decoder is built with
the portable vector backend.

Requirements
************

* The edge264 module must be present in the west workspace
  (``west update`` after this tree's :file:`west.yml` change).
* :zephyr:board:`stm32n6570_dk` (Cortex-M55), or ``native_sim`` for a host build.

Building and Running
********************

The STM32N6570-DK application image is chainloaded and should be built with
sysbuild so the decoder has the full on-chip SRAM:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/video/h264_decode
   :board: stm32n6570_dk
   :west-args: --sysbuild
   :goals: build flash
   :compact:

For serial-boot development without changing BOOT pins, use
``stm32n6570_dk/stm32n657xx/sb``. That variant is limited to about 511 KB of
image size; the default chainloaded target is the supported configuration.

Connect a USB cable to both USB-C ports of the discovery kit for power,
flashing and the console. The on-board LCD shows the decoded test pattern
when RGB565 is accepted by the LTDC; otherwise frames are still logged.

A host build is also available:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/video/h264_decode
   :board: native_sim
   :goals: build run
   :compact:

Sample Output
=============

.. code-block:: console

   Decoder device: video-edge264
   Decoded frame 0: 160x120 RGBP 38400 bytes in 42 ms
   Decoded frame 1: 160x120 RGBP 38400 bytes in 41 ms
