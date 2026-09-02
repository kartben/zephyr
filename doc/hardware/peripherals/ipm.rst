.. _ipm_api:

Inter-Processor Mailbox (IPM)
#############################

Overview
********

An inter-processor mailbox is a hardware block that lets one processor core raise an interrupt on
another core, usually together with a small amount of data. Depending on the SoC it is called a
mailbox, a messaging unit, a doorbell or an IPC peripheral. Some designs carry the payload in
dedicated registers, some only ring a doorbell and leave the data to shared memory, and some are
implemented entirely in software on top of a shared memory region and a cross-core interrupt.

The IPM API is Zephyr's original, minimal abstraction of these blocks. An IPM device is a single
communication endpoint: the application sends a message made of a 32-bit identifier and an
optional payload with :c:func:`ipm_send`, and receives messages through a callback that the driver
invokes from its interrupt handler. The API provides no queuing, no acknowledgment beyond the
remote interrupt handler having run, and no channel concept beyond what the identifier can
express. Higher level protocols such as RPMsg are layered on top of it, and the newer
:ref:`MBOX API <mbox_api>` covers the same hardware with a per-channel model.

Key concepts of the API are:

**IPM device**
  A :c:struct:`device` whose driver implements :c:struct:`ipm_driver_api`. Depending on the
  hardware, one device represents a whole mailbox block, one direction of it, or a single channel.
  The device is obtained from devicetree, for example with :c:macro:`DEVICE_DT_GET` or through
  the ``zephyr,ipc`` chosen node.

**Message identifier**
  The ``id`` argument of :c:func:`ipm_send`, which most drivers deliver unchanged to the receiving
  callback. Its meaning is defined by the driver and the application protocol: it can name a
  channel, a destination processor or a message type. The largest usable value is reported by
  :c:func:`ipm_max_id_val_get`.

**Message data**
  An optional payload of ``size`` bytes. The maximum length, reported by
  :c:func:`ipm_max_data_size_get`, ranges from zero for doorbell-only hardware to a few dozen
  bytes for register-based mailboxes. The length itself is not transmitted: the receiver infers it
  from the identifier or from the protocol.

**Receive callback**
  A function of type :c:type:`ipm_callback_t`, registered with :c:func:`ipm_register_callback`
  and armed with :c:func:`ipm_set_enabled`. It runs in interrupt context and receives the
  identifier together with a pointer to the payload.

**Asynchronous completion**
  With :kconfig:option:`CONFIG_IPM_CALLBACK_ASYNC`, a driver can keep a received message pending
  after the callback returns, until the application acknowledges it with :c:func:`ipm_complete`.

Messages and channels
*********************

The identifier and the payload map onto hardware in driver-specific ways, so protocol code that
must run on several SoCs should query :c:func:`ipm_max_id_val_get` and
:c:func:`ipm_max_data_size_get` at run time rather than assume a layout. The in-tree drivers
illustrate the range of behaviors:

* The NXP LPC mailbox (:dtcompatible:`nxp,lpc-mailbox`) has a single 32-bit data register: the
  identifier must be ``0``, a message carries at most four bytes and a zero word cannot be sent.
* The NXP i.MX Messaging Unit (:dtcompatible:`nxp,imx-mu`) has four 32-bit registers. The
  :kconfig:option:`CONFIG_IPM_IMX_MAX_DATA_SIZE_4`,
  :kconfig:option:`CONFIG_IPM_IMX_MAX_DATA_SIZE_8` and
  :kconfig:option:`CONFIG_IPM_IMX_MAX_DATA_SIZE_16` choice groups them into four, two or one
  message types, so the identifier ranges up to ``3``, ``1`` or ``0`` respectively.
* The STM32 IPCC (:dtcompatible:`st,stm32-ipcc-mailbox`) and HSEM
  (:dtcompatible:`st,stm32-hsem-mailbox`) drivers, the Nordic IPC driver
  (:dtcompatible:`nordic,nrf-ipc`) and the ARM MHU driver (:dtcompatible:`arm,mhu`) are
  doorbell-only and reject or ignore any payload. The identifier selects the IPCC channel, the IPC
  channel or the destination CPU; the HSEM driver accepts only ``0``.
* By default the Nordic driver creates one device named ``IPM_<n>`` per message channel enabled
  with ``CONFIG_IPM_MSG_CH_<n>_ENABLE``, each configured as receive-only or transmit-only through
  the matching ``_RX`` and ``_TX`` options. :kconfig:option:`CONFIG_IPM_NRF_SINGLE_INSTANCE`
  replaces them with a single device in which the identifier selects the channel.
* The ESP32 software mailbox (:dtcompatible:`espressif,esp32-ipm`) copies the payload into the
  region referenced by its ``shared-memory`` property, half of which is available per direction,
  and limits the identifier to 16 bits. It returns ``-ENOMEM`` rather than ``-EMSGSIZE`` for an
  oversized payload.
* The IVSHMEM doorbell driver (:dtcompatible:`linaro,ivshmem-ipm`) uses the identifier as the
  destination peer of the :ref:`inter-VM shared memory device <ivshmem_driver>` and delivers
  callbacks from a dedicated thread sized by
  :kconfig:option:`CONFIG_IPM_IVSHMEM_EVENT_LOOP_STACK_SIZE`. It implements neither
  :c:func:`ipm_max_data_size_get` nor :c:func:`ipm_max_id_val_get`, so these must not be called
  on it.
* The AMD-Xilinx IPI driver (:dtcompatible:`xlnx,zynqmp-ipi-mailbox`) creates one IPM device per
  child agent node and moves up to 32 bytes through the agent's message buffers.
* The :dtcompatible:`zephyr,mbox-ipm` adaptor forwards :c:func:`ipm_send` to an MBOX transmit
  channel, reports the MBOX MTU and channel count as the IPM limits, and passes the MBOX channel
  identifier as the ``id`` of received messages.

:kconfig:option:`CONFIG_IPM_MAX_DATA_SIZE` advertises the platform payload limit at build time. It
defaults to ``0`` for the STM32 IPCC and HSEM drivers and to ``1024`` otherwise; the
:zephyr:code-sample:`openamp-rsc-table` sample uses it to decide whether to attach data to its
notifications.

The ``wait`` argument of :c:func:`ipm_send` asks the driver to busy-wait until the remote side has
consumed the message, which is defined as the remote interrupt handler having finished. Drivers
differ in how they honor it: several ignore it, the ESP32 driver returns ``-EBUSY`` instead of
waiting for the shared memory lock when it is ``0``, and the Intel SEDI driver rejects a
non-waiting send with ``-ENOTSUP``. Any queuing or deferred handshake beyond that has to be
implemented above the API.

Devicetree Configuration
************************

IPM controllers are described in the SoC devicetree files and are usually disabled by default. A
board file or an application overlay enables the node:

.. code-block:: devicetree
   :caption: Mailbox controller node of the NXP LPC55S69

   mailbox0: mailbox@8b000 {
       compatible = "nxp,lpc-mailbox";
       reg = <0x8b000 0xec>;
       interrupts = <31 0>;
       resets = <&reset NXP_SYSCON_RESET(0, 26)>;
       status = "disabled";
   };

.. code-block:: devicetree
   :caption: Enabling the controller and selecting it as the IPC device

   / {
       chosen {
           zephyr,ipc = &mailbox0;
       };
   };

   &mailbox0 {
       status = "okay";
   };

The ``zephyr,ipc`` chosen node is the conventional way for generic code to find the IPM device:
the RPMsg service and the OpenAMP samples read it with ``DT_CHOSEN(zephyr_ipc)``. Platforms whose
hardware exposes separate transmit and receive devices use ``zephyr,ipc_tx`` and ``zephyr,ipc_rx``
instead. Code written for one SoC can also address the node directly with :c:macro:`DEVICE_DT_GET`
and ``DT_NODELABEL``, or look the controller up by compatible with :c:macro:`DEVICE_DT_GET_ANY`.

When the platform only provides an :ref:`MBOX <mbox_api>` driver, the
:dtcompatible:`zephyr,mbox-ipm` binding creates an IPM device on top of a pair of MBOX channels.
The ``mboxes`` property references a transmit and a receive channel, named ``tx`` and ``rx`` in
``mbox-names``:

.. code-block:: devicetree
   :caption: IPM device backed by two channels of an i.MX Messaging Unit MBOX driver

   mailbox_m70_m71_for_m70_as_master: ipm-mbox4 {
       compatible = "zephyr,mbox-ipm";
       mboxes = <&mu_m70_m71_for_m70 1>, <&mu_m70_m71_for_m70 0>;
       mbox-names = "tx", "rx";
   };

Typical application flow
************************

#. Enable :kconfig:option:`CONFIG_IPM`. The driver matching the enabled devicetree node is
   selected automatically.
#. Get the IPM device and check it with :c:func:`device_is_ready`.
#. Query :c:func:`ipm_max_data_size_get` and :c:func:`ipm_max_id_val_get` once and size the
   protocol accordingly.
#. Register a receive callback with :c:func:`ipm_register_callback`.
#. Enable reception with :c:func:`ipm_set_enabled`. Drivers that can mask their receive interrupt
   keep it masked until this call.
#. Send messages with :c:func:`ipm_send`, retrying on ``-EBUSY`` when the remote side has not yet
   consumed the previous message.
#. In the callback, copy the payload out and signal a thread that performs the actual processing.

Basic Operation
***************

The following example sends a 32-bit word to the remote core and waits for its answer. It follows
the :zephyr:code-sample:`ipm-mcux` sample, in which both cores run the same exchange as a
ping-pong loop.

.. code-block:: c
   :caption: Sending a word and waiting for the reply

   #include <zephyr/device.h>
   #include <zephyr/drivers/ipm.h>
   #include <zephyr/kernel.h>
   #include <zephyr/sys/printk.h>

   static K_SEM_DEFINE(rx_sem, 0, 1);
   static uint32_t rx_value;

   static void ipm_rx_callback(const struct device *dev, void *user_data,
                               uint32_t id, volatile void *data)
   {
       ARG_UNUSED(dev);
       ARG_UNUSED(user_data);
       ARG_UNUSED(id);

       /* The pointer is only valid inside the callback: copy the payload out. */
       rx_value = *(volatile uint32_t *)data;
       k_sem_give(&rx_sem);
   }

   int main(void)
   {
       const struct device *const ipm = DEVICE_DT_GET(DT_NODELABEL(mailbox0));
       uint32_t tx_value = 1;
       int ret;

       if (!device_is_ready(ipm)) {
           return -ENODEV;
       }

       if (ipm_max_data_size_get(ipm) < (int)sizeof(tx_value)) {
           return -EMSGSIZE;
       }

       ipm_register_callback(ipm, ipm_rx_callback, NULL);

       ret = ipm_set_enabled(ipm, 1);
       if (ret < 0) {
           return ret;
       }

       ret = ipm_send(ipm, 1, 0, &tx_value, sizeof(tx_value));
       if (ret < 0) {
           return ret;
       }

       k_sem_take(&rx_sem, K_FOREVER);
       printk("Received %u\n", rx_value);

       return 0;
   }

Receive callbacks
=================

The callback executes in the interrupt handler of the mailbox, so it may only use interrupt-safe
kernel services. The ``data`` pointer refers to a temporary buffer owned by the driver or by the
interrupt handler, or to shared memory that the driver locks for the duration of the call; it is
not valid once the callback returns. Copy the bytes the protocol expects and hand them to a
thread, with a semaphore as above or with a work item as the RPMsg backend in
:zephyr_file:`subsys/ipc/rpmsg_service/rpmsg_backend.c` does. Doorbell-only drivers pass a
``NULL`` pointer or a pointer to a dummy word.

:c:func:`ipm_set_enabled` also serves as a flow control mechanism. The IPM console receiver in
:zephyr_file:`drivers/console/ipm_console_receiver.c` disables reception from the callback when
its ring buffer is full and re-enables it from its worker thread once there is room; because the
sender uses a waiting :c:func:`ipm_send`, it blocks until then and no character is lost.

On drivers that split the two directions into separate devices, such as the Nordic driver in its
per-channel mode, :c:func:`ipm_set_enabled` returns ``-EINVAL`` on a transmit-only device and
:c:func:`ipm_send` returns ``-EINVAL`` on a receive-only one.

Sending messages
================

:c:func:`ipm_send` returns ``-EMSGSIZE`` when ``size`` exceeds the driver limit, ``-EINVAL`` for
an identifier above :c:func:`ipm_max_id_val_get` or for a device that is not an outbound channel,
and ``-EBUSY`` when the remote side has not yet read the previous message.

The API does not define whether ``data`` may be ``NULL`` when ``size`` is ``0``, and drivers
differ. Pass a valid buffer whenever the driver reports a non-zero maximum data size, even for
pure notifications: the RPMsg backend sends a dummy word for this reason and only uses a ``NULL``
payload on doorbell-only hardware.

A message may be sent from within the receive callback, which the samples do to echo data back,
but a waiting send in interrupt context stalls the core until the remote handler completes. Real
applications should send from a thread.

:c:func:`ipm_send`, :c:func:`ipm_set_enabled`, :c:func:`ipm_max_data_size_get`,
:c:func:`ipm_max_id_val_get` and :c:func:`ipm_complete` are :ref:`system calls <syscalls>` and can
be used from user threads that were granted access to the device.
:c:func:`ipm_register_callback` is not, so callbacks are registered from supervisor mode.

Asynchronous completion
=======================

Some hardware, such as the Intel SEDI IPC block (:dtcompatible:`intel,sedi-ipm`), does not
acknowledge a message to the sender until the receiver explicitly does so. Drivers for such
hardware select :kconfig:option:`CONFIG_IPM_CALLBACK_ASYNC` and implement the optional
:c:member:`ipm_driver_api.complete` entry. The receive callback is still invoked from the
interrupt handler, but the message and its ``data`` pointer stay valid after it returns, until the
application calls :c:func:`ipm_complete`, typically from a thread once processing has finished. On
drivers without asynchronous support :c:func:`ipm_complete` does nothing, so protocol code can
call it unconditionally.

The API has no power management entry points of its own. A driver that must keep its hardware
powered while a message is in flight marks the device busy (see :ref:`pm-device-busy`); the SEDI
driver does so for both directions.

Relationship with MBOX and the IPC subsystem
********************************************

The :ref:`MBOX API <mbox_api>` is the newer interface for this class of hardware. It exposes each
channel of a controller separately, distinguishes signal-only from data channels and is what the
:ref:`IPC service <ipc_service>` backends build on. New drivers and applications should target
MBOX; the IPM API remains for the code that depends on it:

* The RPMsg service (:kconfig:option:`CONFIG_RPMSG_SERVICE`) and the
  :zephyr:code-sample:`openamp` and :zephyr:code-sample:`openamp-rsc-table` samples use IPM to
  notify the remote side of virtqueue activity.
* The IPM console backend (:kconfig:option:`CONFIG_IPM_CONSOLE`) sends each console line over the
  IPM device chosen as ``zephyr,console``, using the identifier as the line length, while the
  older sender and receiver pair (:kconfig:option:`CONFIG_IPM_CONSOLE_SENDER` and
  :kconfig:option:`CONFIG_IPM_CONSOLE_RECEIVER`) transfers one character per identifier.
  :zephyr_file:`tests/drivers/ipm` exercises the latter with a software-only IPM device.
* The :zephyr:code-sample-category:`ipm` samples demonstrate the raw API on several SoCs.

When a platform only has an MBOX driver, the :dtcompatible:`zephyr,mbox-ipm` adaptor
(:kconfig:option:`CONFIG_IPM_MBOX`) lets this code run unchanged.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_IPM`
* :kconfig:option:`CONFIG_IPM_MAX_DATA_SIZE`
* :kconfig:option:`CONFIG_IPM_CALLBACK_ASYNC`
* :kconfig:option:`CONFIG_IPM_MBOX`
* :kconfig:option:`CONFIG_IPM_NRF_SINGLE_INSTANCE`
* :kconfig:option:`CONFIG_IPM_IMX_MAX_DATA_SIZE`
* :kconfig:option:`CONFIG_IPM_STM32_IPCC_PROCID`
* :kconfig:option:`CONFIG_IPM_STM32_HSEM_CPU`
* :kconfig:option:`CONFIG_IPM_IVSHMEM_EVENT_LOOP_STACK_SIZE`
* :kconfig:option:`CONFIG_IPM_CONSOLE`
* :kconfig:option:`CONFIG_IPM_CONSOLE_SENDER`
* :kconfig:option:`CONFIG_IPM_CONSOLE_RECEIVER`

API Reference
*************

.. doxygengroup:: ipm_interface
