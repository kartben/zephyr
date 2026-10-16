.. _dshot_api:

DShot
#####

Overview
********

DShot is the digital protocol flight controllers use to command the electronic speed controllers
(ESC) of brushless motors. Each ESC has its own signal line, on which the controller sends 16-bit
frames, most significant bit first:

* an 11-bit value: ``0`` stops the motor, ``1`` to ``47`` are special commands, and ``48`` to
  ``2047`` are throttle values,
* a telemetry request bit,
* a 4-bit checksum, the XOR of the three nibbles of the first 12 bits.

Every bit has the same period. The line is high for 37.5 % of the period for a zero and 75 % for
a one, then low for the rest of the period. The bit rate gives the protocol variant its name:

.. list-table::
   :header-rows: 1

   * - Variant
     - Bit rate
     - Bit period
     - Zero high time
     - One high time
     - Frame length
   * - DShot150
     - 150 kbit/s
     - 6.667 µs
     - 2.500 µs
     - 5.000 µs
     - 106.7 µs
   * - DShot300
     - 300 kbit/s
     - 3.333 µs
     - 1.250 µs
     - 2.500 µs
     - 53.3 µs
   * - DShot600
     - 600 kbit/s
     - 1.667 µs
     - 0.625 µs
     - 1.250 µs
     - 26.7 µs
   * - DShot1200
     - 1200 kbit/s
     - 0.833 µs
     - 0.313 µs
     - 0.625 µs
     - 13.3 µs

ESCs detect the bit rate on their own and only arm after receiving motor stop frames for a while,
typically a few hundred milliseconds. They expect frames to keep coming at the control loop rate
and stop the motor when the signal is lost.

Related configuration options:

* :kconfig:option:`CONFIG_DSHOT`
* :kconfig:option:`CONFIG_DSHOT_PULSE_IO`

Basic operation
***************

A DShot device drives a fixed set of channels, one per ESC, all at the same bit rate. The
application obtains it from devicetree and uses the functions in
:zephyr_file:`include/zephyr/drivers/dshot.h`:

* :c:func:`dshot_set_throttle` stages the value of one channel. It only rejects invalid values
  and stores the frame, so it does not block and can be called from an interrupt. Command values
  ``1`` to ``47`` are rejected, so that a computed throttle can never execute a command.
* :c:func:`dshot_send` transmits the staged frame of every channel once. A channel keeps its
  staged value until the next :c:func:`dshot_set_throttle` call. The control loop calls it at its
  update rate.
* :c:func:`dshot_stop` stages a motor stop frame on every channel and transmits it.
* :c:func:`dshot_send_command` sends a special command to a set of channels. The driver applies
  the repetition count, telemetry bit and delays the command needs (see below). Commands are only
  accepted while every channel is stopped, and :c:func:`dshot_set_throttle` refuses nonzero
  values while a command is being sent.
* :c:func:`dshot_configure` changes the bit rate. It is only accepted while every channel is
  stopped, and it fails with ``-ENOTSUP`` when the hardware cannot meet the timing.

.. code-block:: c

   const struct device *esc = DEVICE_DT_GET(DT_NODELABEL(esc));

   /* Arm the ESCs with stop frames */
   for (int i = 0; i < 1000; i++) {
           dshot_send(esc);
           k_msleep(1);
   }

   /* Control loop */
   while (armed) {
           for (uint8_t ch = 0; ch < dshot_get_channel_count(esc); ch++) {
                   dshot_set_throttle(esc, ch, DSHOT_THROTTLE_MIN + mix[ch], false);
           }
           dshot_send(esc);
           k_sleep(K_USEC(500));
   }

   dshot_stop(esc);

The :zephyr:code-sample:`dshot` sample arms a set of ESCs, sends a command and ramps a test
throttle.

Special commands
================

ESCs act on a command only after receiving it in several consecutive frames with the telemetry bit
set, and some commands need time to complete before the next frame. :c:func:`dshot_send_command`
blocks for the whole sequence:

.. list-table::
   :header-rows: 1

   * - Commands
     - Frames
     - Delay after the last frame
   * - Beacons (``1`` to ``5``)
     - 1
     - 260 ms
   * - ESC information (``6``)
     - 6
     - 12 ms
   * - Spin direction, 3D mode, extended telemetry (``7`` to ``10``, ``13``, ``14``, ``20``,
       ``21``)
     - 10
     - 1 ms
   * - Save settings (``12``)
     - 10
     - 35 ms
   * - KISS audio and silent mode toggles (``30``, ``31``)
     - 1
     - 1 ms
   * - Other commands
     - 6
     - 1 ms

The repeated frames are 1 ms apart. The channels that are not addressed receive motor stop
frames.

Pulse IO driver
***************

The :dtcompatible:`zephyr,dshot-pulse-io` driver generates the DShot waveform with :ref:`Pulse IO
<pulse_io_api>` transmit channels, so the hardware times every edge and the CPU only prepares a
buffer per frame. It works on any Pulse IO controller that transmits in one of these modes:

* ``PULSE_IO_MODE_CELL`` with a duty field (``cell_duty_bits`` greater than zero): one cell per
  bit, with the bit period as the cell period. This is the preferred mode.
* ``PULSE_IO_MODE_SYMBOL``: a high and a low symbol per bit.

Controllers whose cells only carry a level cannot generate DShot in CELL mode; the driver uses
SYMBOL mode on them, or fails if they have no SYMBOL mode.

Each entry of the ``pulse-ios`` property is one DShot channel. The driver reserves the Pulse IO
channels when it initializes, so no other driver can use them, and configures them at the
``dshot-speed`` bit rate. The example below drives four ESCs at DShot600 from the ESP32-S3 RMT:

.. code-block:: devicetree

   &pinctrl {
           rmt_dshot: rmt_dshot {
                   group1 {
                           pinmux = <RMT_OUT0_GPIO1>, <RMT_OUT1_GPIO2>,
                                    <RMT_OUT2_GPIO3>, <RMT_OUT3_GPIO4>;
                   };
           };
   };

   &rmt {
           pinctrl-0 = <&rmt_dshot>;
           pinctrl-names = "default";
           status = "okay";
   };

   / {
           esc: dshot {
                   compatible = "zephyr,dshot-pulse-io";
                   pulse-ios = <&rmt 0>, <&rmt 1>, <&rmt 2>, <&rmt 3>;
                   dshot-speed = <600>;
           };
   };

Timing
======

For each channel, the driver asks the controller for its finest tick, capped where the bit period
would no longer fit in the controller duration or duty fields. It reads back the tick rate the
channel actually runs at with :c:func:`pulse_io_channel_get_resolution` and derives the waveform
from it:

* the bit period is the tick rate divided by the bit rate, rounded to the nearest tick,
* the high times are 3/8 and 3/4 of the bit period, rounded to the nearest tick.

The configuration is rejected with ``-ENOTSUP`` when the bit period is shorter than 8 ticks or the
generated bit rate is more than 2 % away from the nominal one. With the 80 MHz tick of the ESP32
RMT, the timing is:

.. list-table::
   :header-rows: 1

   * - Variant
     - Bit period (ticks)
     - High times (ticks)
     - Bit rate error
   * - DShot150
     - 533
     - 200 / 400
     - +0.06 %
   * - DShot300
     - 267
     - 100 / 200
     - −0.12 %
   * - DShot600
     - 133
     - 50 / 100
     - +0.25 %
   * - DShot1200
     - 67
     - 25 / 50
     - −0.50 %

On the NXP FlexIO Pulse IO backend, a channel shifts one bit per tick, and each transmitting
channel interrupts the CPU every 32 ticks. The tick rate is capped by
:kconfig:option:`CONFIG_PULSE_IO_NXP_FLEXIO_MAX_TICK_RATE`, 16 MHz by default, which gives:

.. list-table::
   :header-rows: 1

   * - Variant
     - i.MX RT1170, 200 MHz FlexIO clock (14.29 MHz tick)
     - MCXN947, 150 MHz FlexIO clock (15 MHz tick)
   * - DShot150
     - 95 ticks, high 36 / 71, +0.25 %
     - 100 ticks, high 38 / 75, exact
   * - DShot300
     - 48 ticks, high 18 / 36, −0.79 %
     - 50 ticks, high 19 / 38, exact
   * - DShot600
     - 24 ticks, high 9 / 18, −0.79 %
     - 25 ticks, high 9 / 19, exact
   * - DShot1200
     - 12 ticks, high 5 / 9, −0.79 %
     - not supported (−3.8 %)

Every frame is followed by a low period of at least two bit periods and at least 2 µs (three bit
periods at DShot1200), which ESCs need to tell frames apart. The transmission only completes after
it, so calling :c:func:`dshot_send` back to back cannot break the frame spacing.

Channels and blocking
=====================

:c:func:`dshot_send` transmits the channels one after another with
:c:func:`pulse_io_transmit_sync`, and returns once the last frame and its idle period are out. The
calling thread is blocked for the frame and idle time of every channel plus the Pulse IO driver
overhead, and channel ``n`` starts after channel ``n - 1`` has completed. At DShot600 with four
channels that is at least 4 × (26.7 + 3.3) µs = 120 µs per update, and the last motor is updated
about 90 µs after the first. Pulse IO has no operation that starts several channels together yet,
even on hardware that advertises ``tx_sync_multi_channel``.

Concurrent calls are serialized. A channel whose transmission fails does not prevent the others
from being sent; the first error is returned.

Bidirectional DShot
*******************

In bidirectional DShot, the controller inverts its output (the line idles high), the checksum is
inverted, and the ESC answers every frame about 30 µs later on the same line with a 21-bit GCR
encoded frame at 5/4 of the bit rate. The answer carries the motor electrical period (eRPM) or,
with extended DShot telemetry, temperature, voltage, current and status values.

Bidirectional DShot is not supported. Pulse IO has no half-duplex operation that switches a
channel from transmit to receive within the 30 µs turnaround, and CELL mode cells always start
high, so the inverted waveform needs SYMBOL mode.

Testing safely
**************

* Remove the propellers before connecting an ESC to a board running DShot code.
* Power the ESCs from a current-limited supply when trying new code.
* Send motor stop frames, not throttle values, until the setup is known to work. A logic analyzer
  or an oscilloscope on the signal line shows whether the frames are correct without an ESC.
* Make sure the application stops the motors when it stops sending frames on purpose, and keeps
  sending frames at a steady rate while the motors run.

API Reference
*************

.. doxygengroup:: dshot_interface
