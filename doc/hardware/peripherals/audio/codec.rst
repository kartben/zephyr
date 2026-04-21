.. _audio_codec_api:

Audio Codec
###########

Overview
********

The audio codec interface provides access to audio codec devices.

Audio codecs sit at the boundary between a digital audio stream and the analog
world. Depending on the hardware, a codec can convert PCM playback data into
signals for speakers or headphones, convert microphone input into PCM capture
data, or do both. Many codecs also provide gain control, muting, and internal
signal routing between physical inputs and outputs.

From a Zephyr point of view, an audio codec is a companion device in the audio
path. The application configures the codec's clocking and digital audio
interface, selects whether the codec is used for playback, capture, or both,
and then manages runtime controls such as volume, mute state, and optional
input or output routing.

Key concepts
************

The audio codec API separates setup into a few common pieces:

#. How the codec is clocked,
#. Which digital audio interface format it uses,
#. Whether the stream is used for playback, capture, or both, and
#. Which runtime properties or routes are applied after configuration.

These settings are collected in :c:struct:`audio_codec_cfg` and passed to
:c:func:`audio_codec_configure`.

**Clock and DAI configuration**
  Represented by :c:struct:`audio_codec_cfg`. This describes the codec master
  clock frequency, the digital audio interface type, and the interface-specific
  settings such as PCM sample rate, sample width, channel count, and data
  direction.

**Route selection**
  Also represented by :c:struct:`audio_codec_cfg`. The route field tells the
  driver whether the codec should be prepared for playback, capture, bypass, or
  combined playback and capture use.

**Runtime controls**
  Adjusted with functions such as :c:func:`audio_codec_set_property`,
  :c:func:`audio_codec_apply_properties`, :c:func:`audio_codec_route_input`, and
  :c:func:`audio_codec_route_output`. These APIs let an application change
  per-channel settings like volume or mute state and, on codecs that support
  it, select which physical inputs or outputs are connected to each stream
  channel.

Typical application flow
========================

Typical use of the audio codec API is:

#. Get the codec device, usually from devicetree.
#. Fill an :c:struct:`audio_codec_cfg` structure with clock, interface, and
   route settings.
#. Call :c:func:`audio_codec_configure`.
#. Optionally set properties, apply them, and configure input or output routes.
#. Start playback or capture with :c:func:`audio_codec_start`.
#. Send playback data with :c:func:`audio_codec_write`, or register callbacks to
   be notified when transfers complete.
#. Stop the stream with :c:func:`audio_codec_stop` when finished.

Callbacks and errors
====================

Some codec drivers support optional callbacks for transfer completion and error
reporting. Applications can register these with
:c:func:`audio_codec_register_done_callback` and
:c:func:`audio_codec_register_error_callback` to react to DMA progress or codec
fault conditions during streaming.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_AUDIO_CODEC`

API Reference
*************

.. doxygengroup:: audio_codec_interface
