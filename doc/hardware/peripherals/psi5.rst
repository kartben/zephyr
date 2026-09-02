.. _psi5_api:

Peripheral Sensor Interface (PSI5)
##################################

Overview
********

The Peripheral Sensor Interface (PSI5) is a two-wire, current-modulated bus used in automotive
systems to connect sensors (for example airbag, pressure, or inertial sensors) to an electronic
control unit (ECU). The ECU supplies the sensor over the same two wires and, in synchronous mode,
periodically drives voltage sync pulses on the bus. Sensors answer in fixed time slots after each
sync pulse by modulating the supply current with Manchester-encoded data. Sensors can also transmit
low-rate serial messages over the Serial Messaging Channel (SMC), spread over several consecutive
data frames. In asynchronous mode the sensor transmits without sync pulses and the ECU only
receives.

The Zephyr PSI5 API models the ECU side of the bus. A PSI5 device is a controller with one or more
hardware channels, each connected to a sensor bus. The API abstracts starting and stopping the sync
pulse generator on a channel, sending data to the sensor, and delivering received data and serial
frames to the application through callbacks. Channel timing, frame formats, and receive slots are
static hardware properties and are described in devicetree rather than configured at run time.

Key concepts include:

**Channel**
  Each controller instance exposes several hardware channels, addressed by an integer channel
  index in every API call. A channel is either synchronous (transmit and receive) or asynchronous
  (receive only), as selected by the ``async-mode`` devicetree property.

**Sync pulse generator**
  In synchronous mode the controller generates periodic sync pulses on a channel.
  :c:func:`psi5_start_sync` and :c:func:`psi5_stop_sync` control this generator, and its period is
  set by the ``period-sync-pulse-us`` devicetree property.

**Frames**
  Received messages are delivered as :c:struct:`psi5_frame` values. The
  :c:member:`psi5_frame.type` member (:c:enum:`psi5_frame_type`) tells whether the frame is a data
  frame (:c:enumerator:`PSI5_DATA_FRAME`) or a serial message with a 4-bit or 8-bit ID
  (:c:enumerator:`PSI5_SERIAL_FRAME_4_BIT_ID`, :c:enumerator:`PSI5_SERIAL_FRAME_8_BIT_ID`), and
  therefore which member of the payload union is valid.

**Receive callbacks**
  The application provides receive buffers and callback functions through
  :c:struct:`psi5_rx_callback_config` and registers them with :c:func:`psi5_register_callback`.
  Data frames and serial frames use separate configurations grouped in
  :c:struct:`psi5_rx_callback_configs`.

**Transmission**
  :c:func:`psi5_send` transmits a data word to the sensor on a synchronous channel, either blocking
  until the frame is sent or reporting completion through a :c:type:`psi5_tx_callback_t` callback.

Devicetree Configuration
************************

A PSI5 controller node has one child node per channel, and each channel node has one child node per
receive slot. The generic properties are defined in
:zephyr_file:`dts/bindings/psi5/psi5-controller.yaml`; a vendor binding such as
:dtcompatible:`nxp,s32-psi5` adds controller-specific properties. SoC devicetree files typically
declare all channels and slots as ``disabled``, and a board file or application overlay enables and
configures the ones that are wired to a sensor.

The following example, adapted from the overlays of the :zephyr:code-sample:`psi5` sample, enables
channel 1 of the first controller in synchronous mode, with a 500 us sync pulse period and one
receive slot:

.. code-block:: devicetree

   / {
       aliases {
           psi5-0 = &psi5_0;
       };
   };

   &psi5_0 {
       pinctrl-0 = <&psi5_0_default>;
       pinctrl-names = "default";
       status = "okay";
   };

   &psi5_0_ch1 {
       period-sync-pulse-us = <500>;
       decoder-start-offset-us = <0>;
       pulse-width-0-us = <100>;
       pulse-width-1-us = <127>;
       tx-frame = "long-31-1s";
       num-rx-buf = <32>;
       rx-bitrate-kbps = <189>;
       status = "okay";
   };

   &psi5_0_ch1_rx_slot0 {
       duration-us = <150>;
       start-offset-us = <110>;
       data-length = <16>;
       status = "okay";
   };

At the channel level, ``rx-bitrate-kbps`` selects the receive bit rate (125 or 189 kbps),
``pulse-width-0-us`` and ``pulse-width-1-us`` define the Manchester bit timing used for
transmission, ``tx-frame`` selects the transmit frame length and start condition, and
``decoder-start-offset-us`` keeps the Manchester decoder idle for a short time after the falling
edge of each sync pulse. Each receive slot is placed in time relative to the rising edge of the sync
pulse with ``start-offset-us`` and ``duration-us``, and ``data-length`` gives its payload size in
bits (8 to 28). The optional ``data-msb-first``, ``has-smc`` and ``has-parity`` properties describe
the slot format. ``num-rx-buf`` is specific to :dtcompatible:`nxp,s32-psi5` and sizes the hardware
receive buffer of the channel.

Typical application flow
************************

Typical use of the PSI5 API is:

#. Get the PSI5 controller device, usually from a devicetree alias, and check it with
   :c:func:`device_is_ready`.
#. Declare one or more arrays of :c:struct:`psi5_frame` to receive data and serial frames into.
#. Fill a :c:struct:`psi5_rx_callback_config` for each frame type with the callback, the frame
   buffer, its capacity in frames, and optional user data, and group them in a
   :c:struct:`psi5_rx_callback_configs`.
#. Register the callbacks with :c:func:`psi5_register_callback` for the channel. On a synchronous
   channel this must be done before the sync pulse generator is started.
#. Start the sync pulse generator with :c:func:`psi5_start_sync`. Received frames are now delivered
   to the registered callbacks.
#. Send data to the sensor with :c:func:`psi5_send` when needed.
#. Stop the channel with :c:func:`psi5_stop_sync` when communication is no longer required.

Basic Operation
***************

Receiving frames
================

The driver stores received frames directly in the application-provided buffer referenced by
:c:member:`psi5_rx_callback_config.frame`. When the number of stored frames reaches
:c:member:`psi5_rx_callback_config.max_num_frame`, the driver invokes
:c:member:`psi5_rx_callback_config.callback` with ``num_frame`` equal to that capacity and starts
filling the buffer again from the beginning. Frames must therefore be consumed, or copied out, from
the callback before the next frame arrives.

If the hardware flags a reception error, the driver invokes the callback early with ``num_frame``
set to the number of valid frames stored so far. A callback that receives fewer frames than
``max_num_frame`` should treat the event as an error, as in the :zephyr:code-sample:`psi5` sample.

.. code-block:: c
   :caption: Registering receive callbacks and starting a channel

   #define PSI5_CHANNEL       1
   #define PSI5_MAX_RX_BUFFER 1

   static struct psi5_frame data_frame[PSI5_MAX_RX_BUFFER];

   static void rx_data_frame_cb(const struct device *dev, uint8_t channel,
                                uint32_t num_frame, void *user_data)
   {
       if (num_frame != PSI5_MAX_RX_BUFFER) {
           /* Reception error, data_frame holds num_frame valid frames */
           return;
       }

       /* data_frame[0].data and data_frame[0].timestamp are valid here */
   }

   static struct psi5_rx_callback_config data_cb_cfg = {
       .callback = rx_data_frame_cb,
       .frame = &data_frame[0],
       .max_num_frame = PSI5_MAX_RX_BUFFER,
       .user_data = NULL,
   };

   static struct psi5_rx_callback_configs callback_configs = {
       .serial_frame = NULL,
       .data_frame = &data_cb_cfg,
   };

   int psi5_setup(void)
   {
       const struct device *const dev = DEVICE_DT_GET(DT_ALIAS(psi5_0));
       int err;

       if (!device_is_ready(dev)) {
           return -ENODEV;
       }

       err = psi5_register_callback(dev, PSI5_CHANNEL, callback_configs);
       if (err < 0) {
           return err;
       }

       return psi5_start_sync(dev, PSI5_CHANNEL);
   }

Either member of :c:struct:`psi5_rx_callback_configs` may be ``NULL`` to ignore that frame type.
Calling :c:func:`psi5_register_callback` again replaces both configurations for the channel.

Sending data
============

:c:func:`psi5_send` queues a data word for transmission on a synchronous channel whose sync pulse
generator is running; calling it on a stopped channel returns ``-ENETDOWN``. The ``timeout``
argument bounds how long the call waits for the channel to be ready to accept new data, and
``-EAGAIN`` is returned when it expires.

When ``callback`` is ``NULL`` the call blocks until the frame has been sent and returns the
transmission status. Otherwise the call returns as soon as the frame is queued and the callback is
later invoked with ``status`` set to ``0`` on success or ``-EIO`` on a transmission error:

.. code-block:: c
   :caption: Sending a data word with completion callback

   static void tx_cb(const struct device *dev, uint8_t channel, int status, void *user_data)
   {
       if (status < 0) {
           /* Transmission error */
       }
   }

   err = psi5_send(dev, PSI5_CHANNEL, 0x1234, K_MSEC(100), tx_cb, NULL);

Constraints
===========

* Transmit and receive callbacks are invoked from the controller interrupt handler. They must be
  short and must not block; hand off work to a thread when needed.
* :c:func:`psi5_start_sync`, :c:func:`psi5_stop_sync` and :c:func:`psi5_send` are only meaningful
  on synchronous channels. Starting the generator on an already started channel, or stopping it on
  an already stopped channel, returns ``-EALREADY``. An invalid channel index returns ``-EINVAL``.
* :c:func:`psi5_send` may block up to ``timeout``, and indefinitely when no callback is given, so it
  must be called from thread context.
* All API functions are system calls and may be invoked from user mode threads when
  :kconfig:option:`CONFIG_USERSPACE` is enabled.

The :zephyr:code-sample:`psi5` sample and the test suite in :zephyr_file:`tests/drivers/psi5`
exercise the complete flow on supported hardware.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_PSI5`
* :kconfig:option:`CONFIG_PSI5_INIT_PRIORITY`

API Reference
*************

.. doxygengroup:: psi5_interface
