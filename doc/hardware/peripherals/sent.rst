.. _sent_api:

Single Edge Nibble Transmission (SENT)
######################################

Overview
********

Single Edge Nibble Transmission (SENT, SAE J2716) is a unidirectional, point-to-point protocol used
to transfer sensor data (for example pressure, position or temperature readings) from a sensor to a
controller over a single signal line. The sensor encodes data as a series of pulses: the time
between two consecutive falling edges represents a 4-bit value, called a nibble. Each SENT message
starts with a calibration pulse, which the receiver uses to derive the sensor clock tick, followed
by a status nibble, up to eight data nibbles, and a CRC nibble.

Two logical channels share the same signal line:

* The **fast channel** carries the sensor measurement in the data nibbles of every message.
* The **slow channel** (also called the serial channel) carries a serial message, spread over the
  status nibbles of many consecutive fast messages, in a short format or in enhanced formats with a
  4-bit or 8-bit message ID. It typically transports diagnostic or identification data.

A SENT controller is a receiver peripheral that decodes the pulses on one or more hardware
channels, verifies the CRCs and delivers the decoded messages to software. The Zephyr SENT API
abstracts such a controller: the application registers where received frames should be stored and
which functions to call when they arrive, then starts and stops reception per channel. The protocol
timing parameters and CRC options are configured through devicetree, not at run time.

Key concepts of the API:

**Channel**
  A hardware channel of a SENT controller, identified by a zero-based ``uint8_t`` index that
  matches the ``reg`` value of the corresponding channel child node in devicetree. Every API
  function takes the device and a channel index. An invalid index is rejected with ``-EINVAL``.

**Frame**
  A decoded SENT message, represented by :c:struct:`sent_frame`. Its :c:member:`sent_frame.type`
  member selects which part of the payload union is valid: :c:member:`sent_frame.fast` for
  :c:enumerator:`SENT_FAST_FRAME`, or :c:member:`sent_frame.serial` for the three serial message
  types (:c:enumerator:`SENT_SHORT_SERIAL_FRAME`,
  :c:enumerator:`SENT_ENHANCED_SERIAL_FRAME_4_BIT_ID` and
  :c:enumerator:`SENT_ENHANCED_SERIAL_FRAME_8_BIT_ID`). Every frame also carries a capture
  :c:member:`sent_frame.timestamp` and the received :c:member:`sent_frame.crc`.

**Receive callback configuration**
  A :c:struct:`sent_rx_callback_config` binds a callback of type
  :c:type:`sent_rx_frame_callback_t` to an application-owned frame buffer
  (:c:member:`sent_rx_callback_config.frame`) of :c:member:`sent_rx_callback_config.max_num_frame`
  entries. Two such configurations, one for serial messages and one for fast messages, are grouped
  in :c:struct:`sent_rx_callback_configs` and registered with :c:func:`sent_register_callback`.

**Listening**
  Reception on a channel is enabled with :c:func:`sent_start_listening` and disabled with
  :c:func:`sent_stop_listening`. Frames are only decoded and delivered while the channel is
  listening.

Devicetree Configuration
************************

A SENT controller is described by a node using a binding that includes
:zephyr_file:`dts/bindings/sent/sent-controller.yaml`. Each hardware channel is a child node of the
controller whose ``reg`` property is the channel index. The channel node carries the protocol
parameters that the driver applies when the device is initialized:

``num-data-nibbles``
  Number of data nibbles in a fast message, from 1 to 8 (:c:macro:`SENT_MAX_DATA_NIBBLES`).

``clock-tick-length-us``
  Nominal sensor clock tick length in microseconds. All pulse widths are measured in ticks.

``successive-calib-pulse-method``
  Calibration pulse check method: ``1`` (preferred, high latency) or ``2`` (low latency).

``calib-pulse-tolerance-percent``
  Accepted deviation of the calibration pulse length: ``20`` or ``25`` percent.

``fast-crc`` and ``short-serial-crc``
  CRC algorithm applied to fast and short serial messages, using the ``FAST_CRC_*`` and
  ``SHORT_CRC_*`` flags from :zephyr_file:`include/zephyr/dt-bindings/sent/sent.h`. The fast CRC
  can be disabled with ``FAST_CRC_DISABLE`` or combined with ``FAST_CRC_STATUS_INCLUDE`` to cover
  the status nibble as well.

SoC devicetree files usually define the controller and its channel nodes as disabled. A board or
application overlay enables the controller, assigns the input pin, and configures the channels that
are connected to a sensor. The following example, taken from the :zephyr:code-sample:`sent` sample,
enables channel 1 of the :dtcompatible:`nxp,s32-sent` controller ``sent1``:

.. code-block:: devicetree

   #include <zephyr/dt-bindings/sent/sent.h>

   / {
       aliases {
           sent0 = &sent1;
       };
   };

   &sent1 {
       pinctrl-0 = <&sent1_default>;
       pinctrl-names = "default";
       status = "okay";
   };

   &sent1_ch1 {
       num-data-nibbles = <6>;
       clock-tick-length-us = <3>;
       successive-calib-pulse-method = <2>;
       calib-pulse-tolerance-percent = <20>;
       fast-crc = <FAST_CRC_RECOMMENDED_IMPLEMENTATION>;
       short-serial-crc = <SHORT_CRC_RECOMMENDED_IMPLEMENTATION>;
       status = "okay";
   };

Typical application flow
************************

#. Get the SENT controller device from devicetree, for example with :c:macro:`DEVICE_DT_GET`, and
   check it with :c:func:`device_is_ready`.
#. Allocate one array of :c:struct:`sent_frame` for fast messages and one for serial messages. The
   arrays must stay valid for as long as the callbacks are registered because the driver writes
   received frames into them directly.
#. Fill a :c:struct:`sent_rx_callback_config` for each message type, pointing at the frame buffer,
   its size in frames, the callback function and optional user data.
#. Group both configurations in a :c:struct:`sent_rx_callback_configs` and register them with
   :c:func:`sent_register_callback` for the channel of interest.
#. Enable reception with :c:func:`sent_start_listening`. From this point on, the callbacks are
   invoked as frames arrive.
#. Process frames from the callbacks (or from a thread they notify) and, when reception is no
   longer needed, call :c:func:`sent_stop_listening`.

Basic Operation
***************

The following example, derived from the :zephyr:code-sample:`sent` sample, receives fast and
serial frames on channel 1 of the controller behind the ``sent0`` alias:

.. code-block:: c
   :caption: Receiving SENT fast and serial frames on one channel

   #include <zephyr/drivers/sent/sent.h>

   #define SENT_CHANNEL       1
   #define SENT_MAX_RX_BUFFER 1

   static struct sent_frame serial_frame[SENT_MAX_RX_BUFFER];
   static struct sent_frame fast_frame[SENT_MAX_RX_BUFFER];

   static void rx_serial_frame_cb(const struct device *dev, uint8_t channel,
                                  uint32_t num_frame, void *user_data)
   {
       if (num_frame == SENT_MAX_RX_BUFFER) {
           /* serial_frame[0].serial.id and serial_frame[0].serial.data are valid */
       } else {
           /* Reception error on this channel */
       }
   }

   static void rx_fast_frame_cb(const struct device *dev, uint8_t channel,
                                uint32_t num_frame, void *user_data)
   {
       if (num_frame == SENT_MAX_RX_BUFFER) {
           /* fast_frame[0].fast.data_nibbles[] holds the received nibbles */
       }
   }

   static struct sent_rx_callback_config serial_cb_cfg = {
       .callback = rx_serial_frame_cb,
       .frame = &serial_frame[0],
       .max_num_frame = SENT_MAX_RX_BUFFER,
       .user_data = NULL,
   };

   static struct sent_rx_callback_config fast_cb_cfg = {
       .callback = rx_fast_frame_cb,
       .frame = &fast_frame[0],
       .max_num_frame = SENT_MAX_RX_BUFFER,
       .user_data = NULL,
   };

   int sent_rx_setup(void)
   {
       const struct device *const dev = DEVICE_DT_GET(DT_ALIAS(sent0));
       struct sent_rx_callback_configs callback_configs = {
           .serial = &serial_cb_cfg,
           .fast = &fast_cb_cfg,
       };
       int err;

       if (!device_is_ready(dev)) {
           return -ENODEV;
       }

       err = sent_register_callback(dev, SENT_CHANNEL, callback_configs);
       if (err < 0) {
           return err;
       }

       return sent_start_listening(dev, SENT_CHANNEL);
   }

Receive callbacks
=================

Reception is entirely callback driven; there is no blocking read function. The driver stores each
decoded message in the next free entry of the buffer referenced by
:c:member:`sent_rx_callback_config.frame` and invokes the callback once
:c:member:`sent_rx_callback_config.max_num_frame` frames have been accumulated, passing that count
as the ``num_frame`` argument. Setting ``max_num_frame`` to ``1`` delivers every frame immediately,
while larger values batch several frames per callback. The fast and serial buffers are filled and
counted independently of each other.

When the controller reports a reception error on a channel, the driver invokes the callback with
the number of frames stored so far, which can be lower than ``max_num_frame`` and can be zero, and
restarts filling the buffer from its first entry. A callback should therefore compare ``num_frame``
with the configured buffer size to distinguish a complete batch from an error notification, as the
example above does.

Either member of :c:struct:`sent_rx_callback_configs` may be ``NULL`` to ignore the corresponding
message type. Calling :c:func:`sent_register_callback` again replaces the previous configuration
for that channel, and passing ``NULL`` for both members removes the callbacks.

Constraints
===========

* Callbacks are invoked from the controller's interrupt handler. They must be short and must only
  use APIs that are safe to call from an ISR; longer processing should be deferred to a thread, for
  example through a :c:struct:`k_sem` or a work queue.
* :c:func:`sent_start_listening`, :c:func:`sent_stop_listening` and
  :c:func:`sent_register_callback` are system calls that take a driver-internal lock and must be
  called from thread context. Callbacks can be registered while the channel is listening.
* Starting a channel that is already listening, or stopping one that is already stopped, returns
  ``-EALREADY``. A failure to program the hardware returns ``-EIO``. A driver that does not
  implement an operation returns ``-ENOSYS``.

:zephyr_file:`tests/drivers/sent/sent_api` contains tests exercising the start, stop and callback
registration semantics described above.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_SENT`
* :kconfig:option:`CONFIG_SENT_INIT_PRIORITY`
* :kconfig:option:`CONFIG_SENT_NXP_S32`

API Reference
*************

.. doxygengroup:: sent_interface
