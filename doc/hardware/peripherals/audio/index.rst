.. _audio_reference:

Audio
#####

Overview
********

The Audio subsystem in Zephyr provides a comprehensive set of APIs for working with audio
hardware peripherals. These APIs enable applications to interface with various audio devices
including codecs, microphones, speakers, and digital audio buses.

Audio peripherals covered in this section include:

**Audio Codec**
  Digital audio codec interfaces for encoding and decoding audio streams.
  Support for audio configuration, volume control, and routing.

**DMIC (Digital Microphone)**
  Interface for digital MEMS microphones using PDM (Pulse Density Modulation).
  Support for microphone configuration and audio capture.

**I2S (Inter-IC Sound)**
  Serial audio interface for transferring PCM audio data.
  Support for various audio formats and configurations.

**DAI (Digital Audio Interface)**
  Generic digital audio interface for complex audio pipelines.
  Support for audio streaming and buffering.

Available Audio Peripherals
****************************

.. toctree::
   :maxdepth: 1

   codec.rst
   dmic.rst
   i2s.rst
   dai.rst
