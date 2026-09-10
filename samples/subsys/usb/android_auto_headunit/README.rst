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

USB host
********

With :kconfig:option:`CONFIG_SAMPLE_AA_HU_TRANSPORT_USB_HOST` the board is the USB host and the
phone is the device, which is how a real head unit is wired. A phone enumerates with its own
identity, so the sample first switches it into Android Open Accessory mode: it reads the accessory
protocol version, sends the six identity strings and requests accessory mode. The phone detaches
and comes back as ``18d1:2d00`` with a vendor interface whose bulk endpoints carry the protocol.

The identity strings decide what the phone does. Projection only starts when the manufacturer and
model are the exact strings Android Auto looks for, which is why
:kconfig:option:`CONFIG_SAMPLE_AA_HU_AOA_MODEL` defaults to ``Android Auto``.

.. code-block:: console

   west build -b esp32s3_devkitc/esp32s3/procpu samples/subsys/usb/android_auto_headunit

The board has to supply VBUS to the phone, which the ESP32-S3 development boards do not do on
their USB connector, so power the port externally. The controller is shared between the device and
the host role, so the board configuration turns the device stack off.

This target has no display and decodes into a dummy one. It exists to bring up the host side: the
bundled decoder only understands the companion accessory sample's I_PCM stream, so a real phone
gets as far as a running session but its pictures are not decoded.

The :zephyr:board:`stm32n6570_dk` is a complete head unit: its 800x480 panel is exactly the
resolution the sample offers the phone, its touch screen drives the input channel, and its USB
Type-A connector (CN17) is a host port that supplies VBUS on its own. The image is loaded over the
board's serial boot interface:

.. code-block:: console

   west build -b stm32n6570_dk/stm32n657xx/sb samples/subsys/usb/android_auto_headunit && west flash

The framebuffer and the reassembly buffer are placed in the external PSRAM, since together they
are larger than the internal RAM available to a serial-boot image.

Power the board from an external 5 V supply rather than from the ST-LINK connector. VBUS for the
host connector is drawn from the same rail as the rest of the board, and the ST-LINK port cannot
supply the panel, the external RAM and an attached phone at once.

Hardware YUV scanout on STM32N6
******************************

With the full H.264 decoder enabled, set :kconfig:option:`CONFIG_STM32_LTDC_YUV` to use
hardware color conversion. :kconfig:option:`CONFIG_SAMPLE_AA_HU_LTDC_YUV` then defaults to
enabled. The decoder itself is the ``h264bsd`` module, so the west manifest has to
carry it; :kconfig:option:`CONFIG_SAMPLE_AA_HU_H264` selects it and points its
allocation at the sample's own heap.

The sample interleaves the decoder's I420 planes into packed YUYV 4:2:2, repeating each
chroma row for two luminance rows. The LTDC converts BT.601 limited-range samples to RGB
during scanout. There is no software YUV-to-RGB conversion. Two packed framebuffers use
1,536,000 bytes at 800x480, placed in AXISRAM1 on the STM32N6570-DK. A display worker waits
for VSync while the receive thread decodes the next picture. The old front buffer is reused
only after the swap completes. :kconfig:option:`CONFIG_SAMPLE_AA_HU_YUV_BUFFERS_SECTION`
selects the memory section on other targets.

The STM32N6570-DK configurations optimize for execution speed. Software H.264 decoding
still limits the frame rate for complex scenes; hardware color conversion alone does not
guarantee 30 frames per second.

Direct planar or semiplanar YUV420 scanout is not functional on STM32N6 silicon, as described
in `ES0620, section 2.7.1
<https://www.st.com/resource/en/errata_sheet/es0620-stm32n6xxxx-device-errata-stmicroelectronics.pdf>`_.
The packed format avoids this limitation.

References
**********

* `Android Open Accessory protocol <https://source.android.com/docs/core/interaction/accessories/aoa>`_
