.. zephyr:code-sample:: android-auto-headunit
   :name: Android Auto head unit
   :relevant-api: display_interface input_interface

   Act as an Android Auto head unit: decode the phone's projected screen and show it.

Overview
********

This sample is the counterpart of the :zephyr:code-sample:`android-auto` accessory sample: the
board acts as the car head unit. It connects to a phone that speaks the Android Auto projection
protocol, drives the version exchange, the TLS handshake (as the client) and service discovery,
opens a video and an input channel, decodes the H.264 stream the phone sends and shows it on a
display. The board's own touch screen is forwarded to the phone as touch events.

The protocol, framing, TLS and protobuf code mirror the accessory sample with the roles swapped.
The video path is new: a small decoder reconstructs the picture from the companion sample's
I_PCM-only stream (``src/h264_ipcm_decode.c``). It is the inverse of that sample's encoder and
does not decode a general H.264 stream; a real phone's frames need a full decoder, which a board
with a hardware decoder or an added software decoder module would provide.

Certificates
************

A phone verifies the head unit certificate against Google's certificate authority, so the bundled
self-signed certificate is accepted only by phones that skip that check, such as the companion
accessory sample. Supply a real credential with :kconfig:option:`CONFIG_SAMPLE_AA_HU_CERT_FILE`
and :kconfig:option:`CONFIG_SAMPLE_AA_HU_KEY_FILE` to talk to a real phone.

Building and Running
********************

The sample runs on :zephyr:board:`native_sim` (Linux) with a TCP transport, paired with the
accessory sample as the phone. In one terminal, build and run the accessory sample so it listens
on TCP port 5277:

.. code-block:: console

   west build -b native_sim samples/subsys/usb/android_auto -d build_phone && ./build_phone/zephyr/zephyr.exe

In another terminal, build and run this sample, which connects to it:

.. code-block:: console

   west build -b native_sim samples/subsys/usb/android_auto_headunit -d build_hu && ./build_hu/zephyr/zephyr.exe

With :kconfig:option:`CONFIG_SAMPLE_AA_HU_DEMO_TAP` the head unit also taps the phone's screen
periodically, which drives the accessory sample's counter without a physical touch screen.

The head unit decodes the phone's screen. With :kconfig:option:`CONFIG_SAMPLE_AA_HU_FB_DUMP` it
also writes every decoded picture to ``hu_frames.rgb565``, which

.. code-block:: console

   ffmpeg -f rawvideo -pixel_format rgb565le -video_size 800x480 -i hu_frames.rgb565 frame%03d.png

turns into images. On a Linux desktop with SDL, point ``zephyr,display`` at an SDL display in a
board overlay to watch the stream live and drive the phone with the mouse.

.. code-block:: console

   [00:00:00.010] <inf> aa_control: Phone protocol version 1.1
   [00:00:00.160] <inf> aa_tls: TLS handshake done: TLSv1.2, TLS-ECDHE-ECDSA-WITH-AES-128-GCM-SHA256
   [00:00:00.170] <inf> aa_control: Offering a video and an input channel
   [00:00:00.210] <inf> aa_video: Video stream started, session 0
   [00:00:02.040] <inf> aa_video: Video: 5 frames, 2.4 fps

References
**********

* `Android Open Accessory protocol <https://source.android.com/docs/core/interaction/accessories/aoa>`_
