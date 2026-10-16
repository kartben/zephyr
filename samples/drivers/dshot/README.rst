.. zephyr:code-sample:: dshot
   :name: DShot
   :relevant-api: dshot_interface

   Send DShot frames to brushless motor speed controllers.

Overview
********

This sample drives the electronic speed controllers (ESC) described by the ``dshot0`` devicetree
alias with the :ref:`DShot API <dshot_api>`:

#. It selects the DShot speed set by :kconfig:option:`CONFIG_SAMPLE_DSHOT_SPEED`.
#. It sends motor stop frames on every channel for 3 seconds, so that the ESCs arm.
#. It makes every ESC beep once with the ``DSHOT_CMD_BEACON1`` command, unless
   :kconfig:option:`CONFIG_SAMPLE_DSHOT_BEACON` is disabled.
#. If :kconfig:option:`CONFIG_SAMPLE_DSHOT_THROTTLE` is set, it ramps every motor from the
   lowest throttle value to that value and back down within
   :kconfig:option:`CONFIG_SAMPLE_DSHOT_THROTTLE_MS`, then stops the motors.
#. It keeps sending motor stop frames and prints, every second, how many frames were sent and how
   long :c:func:`dshot_send` took.

Frames are sent at :kconfig:option:`CONFIG_SAMPLE_DSHOT_UPDATE_RATE_HZ` on every channel. By
default the test throttle is ``0`` and the motors never spin.

The DShot device is a :dtcompatible:`zephyr,dshot-pulse-io` node, so the sample runs on any board
with a Pulse IO controller. The devicetree overlay of the board selects the Pulse IO controller,
its channels and their pins.

Requirements
************

* A board with a Pulse IO controller and an overlay for this sample. The overlays provided drive
  four ESCs from:

  * ``esp32s3_devkitc/esp32s3/procpu``: the RMT transmit channels 0 to 3, on GPIO4 to GPIO7.
  * ``mimxrt1170_evk/mimxrt1176/cm7``: FlexIO2, on Arduino header pins D6, D9, D5 and D8.
  * ``frdm_mcxn947/mcxn947/cpu0``: FlexIO0, on Arduino header pins D9, D6, D3 and A4. Its
    15 MHz FlexIO tick cannot generate DShot1200.
* Optionally, ESCs that support DShot, such as ESCs running BLHeli_32, Bluejay or AM32 firmware.
* A logic analyzer or an oscilloscope to check the frames, which is enough to try the sample
  without an ESC.

Wiring
******

Connect the signal input of each ESC to its pin, and the ESC signal ground to the board ground.
Power the ESCs from their own supply. Do not connect the ESC battery voltage or the ESC 5 V output
to the board unless the board is designed for it.

.. warning::

   Remove the propellers before running this sample with motors connected, and use a
   current-limited supply. A wrong throttle value or a bug can spin a motor at full speed.

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/dshot
   :board: esp32s3_devkitc/esp32s3/procpu
   :goals: build flash
   :compact:

To spin the motors, for example ramping to throttle 200 at DShot300:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/dshot
   :board: esp32s3_devkitc/esp32s3/procpu
   :goals: build flash
   :gen-args: -DCONFIG_SAMPLE_DSHOT_THROTTLE=200 -DCONFIG_SAMPLE_DSHOT_SPEED=300
   :compact:

To run the sample on another board, add an overlay that defines a
:dtcompatible:`zephyr,dshot-pulse-io` node, routes its Pulse IO channels to pins and points the
``dshot0`` alias at it. See :zephyr_file:`samples/drivers/dshot/boards/esp32s3_devkitc_procpu.overlay`.

Sample Output
=============

.. code-block:: console

   DShot600 on 4 channels
   motor stop frame 0x0000
   arming: sending motor stop frames for 3000 ms
   armed
   frames sent: 3000, errors: 0, dshot_send() average <t> us, max <t> us
   beacon command sent (0), frame 0x0033
   frames sent: 4000, errors: 0, dshot_send() average <t> us, max <t> us

The ``dshot_send()`` times depend on the board, the speed and the number of channels. With
:kconfig:option:`CONFIG_DSHOT_LOG_LEVEL_DBG`, the driver also logs the mode, tick rate and bit
timing it uses on every channel.

Checking the frames with a logic analyzer
*****************************************

Probe the signal pins at 24 MHz or more (100 MHz or more for DShot1200) and use a DShot or a
pulse width decoder:

* Bit period: 6.667, 3.333, 1.667 or 0.833 µs for DShot150, 300, 600 and 1200, within 2 %.
* High time: 37.5 % of the bit period for a zero and 75 % for a one, quantized to the Pulse IO
  tick: 12.5 ns on the ESP32 RMT, 70 ns on the i.MX RT1170 FlexIO.
* Frames: 16 bits, followed by at least two bit periods and 2 µs low. Motor stop frames are
  sixteen zeros (``0x0000``); the beacon frames read ``0x0033``.
* Frame rate and jitter: frames start every 1 ms at the default update rate. The jitter between
  frames comes from the thread scheduling, not from the waveform, which the hardware generates.
* Skew between channels: compare the first rising edge of every channel in one update. Channels
  of a controller that reports ``tx_sync_multi_channel``, such as the ESP32-S3 RMT and the FlexIO
  backend, start on the same tick edge, so their first edges should be within one tick of each
  other.
