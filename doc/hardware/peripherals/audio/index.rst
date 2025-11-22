.. _audio_reference:

Audio
#####

Overview
********

The Audio subsystem in Zephyr provides a comprehensive set of APIs for working with audio
hardware peripherals and building complete audio systems. These APIs enable applications to
interface with various audio devices including codecs, microphones, speakers, and digital
audio buses.

Zephyr's audio support is designed to be modular and flexible, allowing developers to build
audio applications ranging from simple audio playback to complex multi-channel audio processing
systems with minimal overhead.

Audio Architecture
******************

The Zephyr audio subsystem consists of several layers that work together:

Digital Audio Interfaces
========================

At the lowest level are the hardware interfaces that transfer audio data:

* **I2S (Inter-IC Sound)** - Serial interface for stereo PCM audio data
* **DAI (Digital Audio Interface)** - Generic interface for various digital audio formats

These interfaces handle the actual transmission and reception of audio samples between
components like DSPs, codecs, and memory.

Audio Codecs
============

Audio codecs convert between analog and digital audio signals. The Audio Codec API provides:

* Configuration of audio parameters (sample rate, bit depth, channels)
* Control of input/output routing
* Volume and mute controls
* Power management

Digital Microphones
===================

Digital microphones (DMIC) use PDM (Pulse Density Modulation) to convert acoustic signals
directly to digital form. The DMIC API supports:

* Multi-channel microphone arrays
* Configurable sample rates and bit depths
* Hardware filtering and decimation
* Low-power operation modes

Audio Components
****************

.. _audio_codec_component:

Audio Codec
===========

The Audio Codec API provides access to hardware audio codecs that convert between analog and
digital audio. Codecs typically include:

* ADCs (Analog-to-Digital Converters) for recording
* DACs (Digital-to-Analog Converters) for playback
* Amplifiers for speakers and headphones
* Input/output mixing and routing

Common use cases:

* Audio playback through speakers or headphones
* Audio recording from microphones or line inputs
* Full-duplex audio (simultaneous playback and recording)
* Voice communication applications

.. _audio_dmic_component:

Digital Microphone (DMIC)
=========================

Digital microphones output audio data in PDM (Pulse Density Modulation) format, which must be
filtered and decimated to produce standard PCM audio samples. The DMIC API handles:

* PDM interface configuration
* CIC filter configuration for decimation
* Multi-microphone arrays (stereo, quad, etc.)
* Synchronous capture from multiple microphones

Common use cases:

* Voice recognition and speech processing
* Acoustic event detection
* Beamforming with microphone arrays
* High-quality audio recording

.. _audio_i2s_component:

I2S (Inter-IC Sound)
====================

I2S is a standard serial bus interface for transferring digital audio data between integrated
circuits. The I2S API in Zephyr supports:

* Standard I2S (Philips) format
* Left-justified and right-justified formats
* PCM short and long frame sync
* Controller and target modes
* Various word sizes (8, 16, 24, 32 bits)

I2S is commonly used to connect:

* Audio codecs to processors
* DSPs to audio peripherals
* Multiple audio processing components in a pipeline

.. _audio_dai_component:

DAI (Digital Audio Interface)
=============================

The DAI API provides a generic high-level interface for digital audio that can be configured
for vendor-specific protocols and formats. DAI supports:

* Multiple audio formats and protocols
* Configurable data lanes and channels
* Integration with DMA for efficient data transfer
* Time Division Multiplexing (TDM) support

DAI is particularly useful for:

* Complex audio pipelines with multiple stages
* Custom or proprietary audio interfaces
* Multi-channel audio systems (> 2 channels)
* Professional audio equipment

Working with Audio Data
***********************

Sample Formats
==============

Audio samples in Zephyr are typically represented as:

* **PCM (Pulse Code Modulation)** - Linear representation of audio amplitude
* **PDM (Pulse Density Modulation)** - 1-bit high-frequency stream (DMIC output)

Common PCM formats:

* 16-bit signed integer (most common)
* 24-bit signed integer (high quality)
* 32-bit signed integer or floating point (professional)

Sample Rates
============

Standard audio sample rates supported:

* 8 kHz - Narrowband voice
* 16 kHz - Wideband voice  
* 44.1 kHz - CD quality audio
* 48 kHz - Professional audio, DAT
* 96 kHz, 192 kHz - High-resolution audio

Audio Channels
==============

Common channel configurations:

* Mono (1 channel) - Single audio source
* Stereo (2 channels) - Left and right
* Multi-channel (4, 6, 8+ channels) - Surround sound, microphone arrays

Buffer Management
=================

Audio applications must manage buffers carefully to avoid:

* Underruns (playback) - Buffer empties before new data arrives
* Overruns (capture) - Buffer fills before data is consumed

Use appropriate buffer sizes based on:

* Sample rate and bit depth
* Processing latency requirements
* Memory constraints
* DMA capabilities

Typical Audio System Examples
*****************************

Simple Audio Playback
=====================

A minimal audio playback system:

#. Configure audio codec for DAC output
#. Set sample rate and format (e.g., 48 kHz, 16-bit stereo)
#. Configure I2S for controller mode
#. Fill audio buffers with PCM samples
#. Start I2S transmission to send data to codec
#. Continuously provide new buffers to maintain playback

Voice Capture
=============

A basic voice recording system:

#. Configure DMIC for mono or stereo capture
#. Set sample rate (e.g., 16 kHz for voice)
#. Allocate receive buffers
#. Start DMIC capture
#. Process received PDM data (already converted to PCM by hardware)
#. Store or transmit captured audio

Full-Duplex Communication
=========================

A voice call or intercom system:

#. Configure codec for both ADC (microphone) and DAC (speaker)
#. Set up I2S for simultaneous transmit and receive
#. Use separate buffers for playback and capture paths
#. Process audio in real-time (echo cancellation, noise reduction)
#. Maintain synchronization between capture and playback

Configuration Options
*********************

Audio subsystem configuration options:

* :kconfig:option:`CONFIG_AUDIO` - Enable audio subsystem support
* :kconfig:option:`CONFIG_AUDIO_CODEC` - Enable audio codec support
* :kconfig:option:`CONFIG_AUDIO_DMIC` - Enable digital microphone support  
* :kconfig:option:`CONFIG_I2S` - Enable I2S interface support
* :kconfig:option:`CONFIG_DAI` - Enable DAI interface support

See individual peripheral documentation for device-specific options.

API Documentation
*****************

.. toctree::
   :maxdepth: 1

   codec.rst
   dmic.rst
   i2s.rst
   dai.rst
