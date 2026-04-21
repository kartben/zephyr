.. _audio_dmic_api:

Digital Microphone (DMIC)
#########################

Overview
********

The audio DMIC interface provides access to digital microphones.

A digital microphone typically outputs a PDM (Pulse Density Modulation) bit
stream instead of analog audio. A DMIC controller receives that PDM data,
applies decimation and filtering in hardware, and exposes PCM audio samples to
software.

From a Zephyr point of view, a DMIC device is an audio capture peripheral. The
application configures the microphone and controller, starts capture, and reads
PCM buffers from the driver.

Key concepts
************

The main concepts in the DMIC API are:

* **PDM I/O configuration** describes the microphone-facing signal
  requirements, such as supported clock frequency and duty cycle.
* **Channel configuration** describes how physical PDM left/right channels are
  mapped to logical PCM channels, and how many channels and streams are active.
* **PCM stream configuration** describes the capture output, including sample
  rate, sample width, block size, and the memory slab used for receive buffers.

Typical application flow
========================

Typical use of the DMIC API is:

#. Get the DMIC device, usually from devicetree.
#. Fill a :c:struct:`dmic_cfg` structure with I/O, channel, and stream
   settings.
#. Call :c:func:`dmic_configure`.
#. Start capture with :c:func:`dmic_trigger` using
   :c:enumerator:`DMIC_TRIGGER_START`.
#. Fetch PCM data with :c:func:`dmic_read`.
#. Stop, pause, or reset capture with additional trigger commands as needed.

Devicetree and buffering
========================

The microphone description in devicetree can provide the static PDM
capabilities of the attached microphone, such as minimum and maximum clock
frequency, duty cycle limits, and whether left and/or right channels are
present.

Received PCM data is returned through buffers owned by the driver. Applications
provide the backing memory through a :c:struct:`k_mem_slab` referenced by each
configured PCM stream.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_AUDIO_DMIC`

API Reference
*************

.. doxygengroup:: audio_dmic_interface
