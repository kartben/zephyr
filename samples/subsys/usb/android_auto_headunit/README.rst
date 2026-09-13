.. zephyr:code-sample:: android-auto-headunit
   :name: Android Auto head unit
   :relevant-api: display_interface input_interface

   Act as an Android Auto head unit: decode the phone's projected screen and show it.

Overview
********

The board acts as the car head unit. It connects to a phone that speaks the Android Auto
projection protocol, drives the version exchange, the TLS handshake as the client and service
discovery, then opens the channels a head unit offers:

* **Video.** The phone's H.264 stream is decoded with the ``h264bsd`` module and shown on the
  board's display.
* **Input.** The board's touch screen is forwarded to the phone, with multiple pointers where the
  controller reports them.
* **Audio out.** The media, guidance and system streams the phone sends are mixed and played
  through the board's codec.
* **Microphone.** The board's digital microphone is captured and sent back, which is what the
  phone's voice assistant listens to.
* **Sensors.** The head unit reports its light level, gear and parking brake. The phone restricts
  projection until it can tell the car is stationary, and it redraws everything in its night theme
  while the head unit reports darkness.
* **Navigation status.** The next turn is drawn on a small secondary display, if the board has one.

Certificates
************

A phone verifies the head unit certificate against Google's certificate authority, which does not
issue to the public. The bundled ``certs/hu_cert.pem`` is self-signed, so a phone drops the
connection during the handshake: point :kconfig:option:`CONFIG_SAMPLE_AA_HU_CERT_FILE` and
:kconfig:option:`CONFIG_SAMPLE_AA_HU_KEY_FILE` at a head unit credential to get past it.

The sample does not verify the phone's certificate in return, which keeps a root store out of it
and lets a session be read back from a capture.

STM32N6570-DK
*************

The :zephyr:board:`stm32n6570_dk` is a complete head unit: its 800x480 panel is exactly the
resolution the sample offers the phone, its touch screen drives the input channel, its digital
microphone and audio codec carry the voice assistant, and its USB Type-A connector (CN17) is a
host port that supplies VBUS on its own.

.. code-block:: console

   west build -b stm32n6570_dk/stm32n657xx/sb samples/subsys/usb/android_auto_headunit && west flash

Power the board from an external 5 V supply rather than from the ST-LINK connector. VBUS for the
host connector is drawn from the same rail as the rest of the board, and the ST-LINK port cannot
supply the panel, the external RAM and an attached phone at once.

A phone enumerates with its own identity, so the sample first switches it into Android Open
Accessory mode: it reads the accessory protocol version, sends the six identity strings and
requests accessory mode. The phone detaches and comes back as ``18d1:2d00`` with a vendor
interface whose bulk endpoints carry the protocol. The identity strings decide what the phone
does; projection only starts when the manufacturer and model are the exact strings Android Auto
looks for, which is why :kconfig:option:`CONFIG_SAMPLE_AA_HU_AOA_MODEL` defaults to
``Android Auto``.

The next turn is shown on an M5Stack Unit Mini OLED wired to the Arduino header, SCL on D15 and
SDA on D14. Without it the navigation channel is still opened and the turns are logged.

.. code-block:: console

   [00:00:01.120] <inf> aa_control: Phone protocol version 1.1
   [00:00:01.320] <inf> aa_tls: TLS handshake done: TLSv1.2, TLS-ECDHE-RSA-WITH-AES-128-GCM-SHA256
   [00:00:01.460] <inf> aa_control: Offering 7 channels
   [00:00:02.010] <inf> aa_video: Video stream started, session 0
   [00:00:04.020] <inf> aa_video: Video: 59 frames, 29.7 fps

Where the pictures go
=====================

The decoder is limited by processor time, not by the link or the panel, so the sample spends the
board's fast memory on it. The reference pictures go in AXISRAM3 to AXISRAM6, four contiguous
banks the board otherwise leaves off, which is worth roughly two thirds of the frame rate over
reading them from the external PSRAM. The framebuffer and the reassembly buffer stay in PSRAM.

Rather than convert each picture in software, the sample interleaves the decoder's planar I420
output into packed YUYV 4:2:2 and lets the display controller convert it to RGB during scanout
(:kconfig:option:`CONFIG_STM32_LTDC_YUV`, which makes
:kconfig:option:`CONFIG_SAMPLE_AA_HU_LTDC_YUV` default on). The interleave is a byte shuffle with
no arithmetic in it. Two packed framebuffers use 1,536,000 bytes at 800x480 and go in AXISRAM1;
:kconfig:option:`CONFIG_SAMPLE_AA_HU_YUV_BUFFERS_SECTION` moves them elsewhere on other targets.
Planar and semiplanar YUV420 scanout is not functional on STM32N6 silicon, as described in
`ES0620, section 2.7.1
<https://www.st.com/resource/en/errata_sheet/es0620-stm32n6xxxx-device-errata-stmicroelectronics.pdf>`_;
the packed format avoids it.

Running against a phone on the host
***********************************

The Android Auto application can serve the same protocol over a socket instead of over USB, which
is how Google's Desktop Head Unit is driven. That makes the sample runnable on
:zephyr:board:`native_sim`, without a board, against a real phone.

Enable developer mode in the Android Auto application, start its head unit server, and forward the
port to the machine that runs the sample:

.. code-block:: console

   adb forward tcp:5277 tcp:5277

Then build and run, pointing the sample at a head unit credential as above:

.. code-block:: console

   west build -b native_sim samples/subsys/usb/android_auto_headunit \
     -- -DCONFIG_SAMPLE_AA_HU_CERT_FILE=\"/path/to/headunit.crt\" \
        -DCONFIG_SAMPLE_AA_HU_KEY_FILE=\"/path/to/headunit.key\"
   ./build/zephyr/zephyr.exe

:kconfig:option:`CONFIG_SAMPLE_AA_HU_TCP_HOST` and :kconfig:option:`CONFIG_SAMPLE_AA_HU_TCP_PORT`
say where to connect; the default is the forwarded port on the loopback address. The address is
parsed as a literal, so a container has to be given the numeric address of the machine holding the
phone, or be run on its network namespace so that the default reaches it.

The head unit's panel is the SDL window, sized to 800x480 by the board overlay, and its mouse is
the touch screen: what the phone projects appears in the window and clicking it drives the phone.

On a machine with no display to open a window on, build with ``headless.overlay`` and
``CONFIG_SDL_DISPLAY=n`` to decode into a dummy display instead, as the sample's own test scenario
does. :kconfig:option:`CONFIG_SAMPLE_AA_HU_FB_DUMP` then writes every picture to
``hu_frames.rgb565``, which

.. code-block:: console

   ffmpeg -f rawvideo -pixel_format rgb565le -video_size 800x480 -i hu_frames.rgb565 frame%03d.png

turns into images, and :kconfig:option:`CONFIG_SAMPLE_AA_HU_DEMO_TAP` taps a fixed point every few
seconds so that the input channel does something without a mouse.

References
**********

* `Android Open Accessory protocol <https://source.android.com/docs/core/interaction/accessories/aoa>`_
