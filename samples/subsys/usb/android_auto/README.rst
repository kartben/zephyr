.. zephyr:code-sample:: android-auto
   :name: Android Auto accessory
   :relevant-api: usbd_api display_interface input_interface

   Stream the LVGL screen to an Android Auto head unit and receive its touch events.

Overview
********

This sample makes a Zephyr board look like an Android phone running Android Auto. The head unit
(the car, or an emulator on a computer) is the USB host: it puts the board into Android Open
Accessory mode, negotiates a session over the accessory bulk endpoints and displays the video
stream the board sends, which is the LVGL screen of the sample. Touch events on the head unit come
back and drive the LVGL widgets.

The pieces involved are:

* A USB function that answers the Android Open Accessory requests (protocol version, identity
  strings, accessory start) and exposes the vendor interface with one bulk IN and one bulk OUT
  endpoint that the head unit expects (``src/aa_aoa.c``, ``src/aa_transport_usb.c``).
* The projection protocol: framing, a TLS 1.2 server whose records travel inside protocol
  messages, protobuf control messages (nanopb), service discovery, video and input channels
  (``src/aa_frame.c``, ``src/aa_tls.c``, ``src/aa_session.c``, ``src/aa_control.c``,
  ``src/aa_video.c``, ``src/aa_input.c``).
* A virtual display driver that LVGL renders into and a touch input device fed by the head unit,
  both wired through the standard LVGL glue with the ``android-auto-display`` and
  ``android-auto-touch`` nodes of ``dts/common/android_auto.dtsi`` (``src/aa_display.c``,
  ``src/aa_touch.c``).
* A minimal H.264 encoder: every macroblock is coded either as raw samples (I_PCM) or skipped, so
  the picture the head unit decodes is exactly the framebuffer. Only macroblocks that changed
  since the previous picture are sent (``src/h264_ipcm.c``, ``src/yuv.c``, ``src/aa_encoder.c``).
* A TCP transport used instead of USB on :zephyr:board:`native_sim`, listening on the port the
  Android SDK Desktop Head Unit connects to by default (``src/aa_transport_tcp.c``).
* A virtual head unit written in Python (``scripts/aa_headunit.py``) that connects over TCP or
  USB, logs the protocol exchange, decodes the video and sends mouse events back as touches.

Compatibility
*************

Android Auto uses TLS with mutual authentication: the head unit presents a certificate issued by
Google and verifies that the phone certificate is issued by Google as well. The sample can only
present the self-signed certificate in ``certs/`` (or one supplied with
:kconfig:option:`CONFIG_SAMPLE_AA_CERT_FILE` and :kconfig:option:`CONFIG_SAMPLE_AA_KEY_FILE`),
so head units that verify the phone certificate drop the connection during the handshake.

.. list-table::
   :header-rows: 1

   * - Head unit
     - Result
   * - ``scripts/aa_headunit.py`` (TCP or USB)
     - Works, this is the reference for the sample
   * - Open-source head units that do not verify the phone certificate
     - Expected to work
   * - Android SDK Desktop Head Unit (``desktop-head-unit``, TCP or ``--usb``)
     - Session starts, the handshake is expected to fail on the certificate
   * - Cars
     - The handshake is expected to fail on the certificate

The protocol constants were written from the observed wire format and are exercised against the
virtual head unit only; other head units may differ in details.

Requirements
************

Building needs ``protoc`` on the path for nanopb. Running the virtual head unit needs Python 3.12
or newer with ``pyusb`` (USB, plus ``libusb``), ``PyAV`` and ``pygame`` (video window), see
``scripts/requirements.txt``. ``ffmpeg`` is useful to inspect a dumped stream.

The video stream needs a framebuffer at the stream resolution (800x480 by default) or at half of
it, in which case every framebuffer pixel becomes a 2x2 block. An RGB565 framebuffer takes 768 KiB
at full resolution and 192 KiB at half resolution; the grayscale option
(:kconfig:option:`CONFIG_SAMPLE_AA_PIXEL_L8`) halves that. The provided board files use:

* :zephyr:board:`native_sim`: TCP transport, full resolution in colour.
* :zephyr:board:`esp32s3_devkitc` and :zephyr:board:`m5stack_stamps3`: USB, half resolution in
  grayscale, which fits the internal RAM next to TLS and USB. On StampS3 the console moves to
  UART0 because USB OTG takes over the USB PHY from the USB serial console.
* :zephyr:board:`twatch_s3`: USB, full resolution in colour with the framebuffer in PSRAM. The
  board has no UART, so the USB serial console only works until USB OTG takes over the PHY;
  :kconfig:option:`CONFIG_SAMPLE_AA_USB_START_DELAY_MS` delays that by a few seconds after every
  reset, which shows the boot log and leaves a window for ``west flash``.
* ``esp32s3_devkitc`` modules with PSRAM: build with ``-DFILE_SUFFIX=psram`` for full resolution
  in colour.

Building and Running
********************

native_sim and the virtual head unit
====================================

:zephyr:board:`native_sim` runs on Linux. Build and run the sample:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/usb/android_auto
   :board: native_sim
   :goals: build run
   :compact:

Then start the virtual head unit on the same machine (or on another one, replacing
``localhost``):

.. code-block:: console

   pip install -r samples/subsys/usb/android_auto/scripts/requirements.txt
   python3 samples/subsys/usb/android_auto/scripts/aa_headunit.py --tcp localhost:5277

A window shows the LVGL screen; clicking and dragging in it drives the widgets, ``n`` and ``d``
switch the night mode when the head unit was started with ``--sensors``. ``--no-display`` runs
without decoding the video and ``--dump FILE`` writes the raw H.264 stream, which ``ffplay -f
h264 FILE`` plays back.

The Android SDK Desktop Head Unit (``extras/google/auto/desktop-head-unit`` in the SDK
directory) connects to ``localhost:5277`` when started without arguments and can be tried the
same way.

USB on ESP32-S3 boards
======================

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/usb/android_auto
   :board: esp32s3_devkitc/esp32s3/procpu
   :goals: build flash
   :compact:

Connect the USB OTG port of the board to the computer running the virtual head unit. The board
enumerates as ``18d1:2d00``, the ids of an Android device in accessory mode, and the head unit
finds it without arguments:

.. code-block:: console

   python3 samples/subsys/usb/android_auto/scripts/aa_headunit.py --usb

With :kconfig:option:`CONFIG_SAMPLE_AA_USB_ACCESSORY_ON_START` the board enumerates with the
Zephyr vendor and product ids instead and only switches to accessory mode when the head unit
asks for it; ``--aoa 2fe3:0014`` makes the virtual head unit perform that exchange. The Desktop
Head Unit does the same with ``desktop-head-unit --usb``.

Sample output
=============

.. code-block:: console

   [00:00:00.000,000] <inf> aa_display: Virtual display 800x480 (RGB565), stream 800x480, scale 1
   [00:00:00.010,000] <inf> aa_encoder: H.264 800x480, 1500 macroblocks, 1 rows per slice, SPS+PPS 29 bytes
   [00:00:00.020,000] <inf> aa_tcp: Listening on TCP port 5277
   [00:00:00.020,000] <inf> aa_session: Android Auto sample ready (transport tcp)
   [00:00:05.000,000] <inf> aa_tcp: Head unit connected
   [00:00:05.000,000] <inf> aa_control: Head unit protocol version 1.1
   [00:00:05.100,000] <inf> aa_tls: TLS handshake done: TLSv1.2, TLS-ECDHE-ECDSA-WITH-AES-128-GCM-SHA256
   [00:00:05.100,000] <inf> aa_control: Authenticated, encryption on
   [00:00:05.110,000] <inf> aa_control: Head unit "Zephyr virtual head unit", car model "Universal", 2 channels
   [00:00:05.120,000] <inf> aa_video: Video setup ok, 1 unacknowledged media messages allowed
   [00:00:05.130,000] <inf> aa_video: Streaming 800x480 to the head unit

Checking the encoder
====================

On :zephyr:board:`native_sim`, :kconfig:option:`CONFIG_SAMPLE_AA_H264_DUMP` writes the stream to
``aa_video.h264`` and every picture, converted from the framebuffer with the same code, to
``aa_video.h264.yuv``. With :kconfig:option:`CONFIG_SAMPLE_AA_H264_SELFTEST` a few pictures are
encoded at boot without a head unit. Decoding the dump with ``ffmpeg`` must reproduce the
reference pictures exactly:

.. code-block:: console

   west build -b native_sim samples/subsys/usb/android_auto -- \
     -DCONFIG_SAMPLE_AA_H264_DUMP=y -DCONFIG_SAMPLE_AA_H264_SELFTEST=y
   ./build/zephyr/zephyr.exe
   ffmpeg -loglevel error -i aa_video.h264 -f rawvideo -pix_fmt yuv420p decoded.yuv
   cmp decoded.yuv aa_video.h264.yuv && echo identical

Configuration
*************

The main options, all under "Android Auto sample" in ``menuconfig``:

.. list-table::
   :header-rows: 1

   * - Option
     - Purpose
   * - :kconfig:option:`CONFIG_SAMPLE_AA_TRANSPORT_USB` / :kconfig:option:`CONFIG_SAMPLE_AA_TRANSPORT_TCP`
     - Talk to the head unit over the accessory endpoints or over TCP
       (:kconfig:option:`CONFIG_SAMPLE_AA_TCP_PORT`).
   * - :kconfig:option:`CONFIG_SAMPLE_AA_USB_ACCESSORY_AT_BOOT`
     - Enumerate directly with the accessory ids, or wait for the accessory start request.
   * - :kconfig:option:`CONFIG_SAMPLE_AA_VIDEO_800X480` / :kconfig:option:`CONFIG_SAMPLE_AA_VIDEO_1280X720`
     - Stream resolution; the framebuffer size comes from the display node in devicetree.
   * - :kconfig:option:`CONFIG_SAMPLE_AA_PIXEL_L8`
     - Grayscale framebuffer for boards without external RAM.
   * - :kconfig:option:`CONFIG_SAMPLE_AA_MAX_FPS`, :kconfig:option:`CONFIG_SAMPLE_AA_MAX_MBS_PER_FRAME`,
       :kconfig:option:`CONFIG_SAMPLE_AA_MAX_KBPS`
     - Pace the stream: pictures per second, coded macroblocks per picture, output rate.
   * - :kconfig:option:`CONFIG_SAMPLE_AA_H264_SLICE_ROWS`
     - Macroblock rows per slice, 0 for a single slice per picture.
   * - :kconfig:option:`CONFIG_SAMPLE_AA_SENSOR_CHANNEL`
     - Follow the head unit night mode with the LVGL dark theme.
   * - :kconfig:option:`CONFIG_SAMPLE_AA_CERT_FILE`, :kconfig:option:`CONFIG_SAMPLE_AA_KEY_FILE`
     - Credentials presented to the head unit (PEM).
   * - :kconfig:option:`CONFIG_SAMPLE_AA_DEMO_WIDGETS`
     - Show the LVGL widgets demo instead of the sample screen.

A full picture is about 580 KiB, which takes half a second on a full-speed USB link, so the
sample sends the whole screen once and then only the macroblocks that changed. The pace of large
redraws can be limited with :kconfig:option:`CONFIG_SAMPLE_AA_MAX_MBS_PER_FRAME` and
:kconfig:option:`CONFIG_SAMPLE_AA_MAX_KBPS`.

References
**********

* `Android Open Accessory protocol <https://source.android.com/docs/core/interaction/accessories/aoa>`_
* `Android Auto Desktop Head Unit <https://developer.android.com/training/cars/testing/dhu>`_
