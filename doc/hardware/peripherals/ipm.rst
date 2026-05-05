.. _ipm_api:

Inter-Processor Mailbox (IPM)
#############################

Overview
********

Inter-Processor Mailbox (IPM) is a communication mechanism for exchanging messages between
different processors or cores in a multi-core system. IPM provides a lightweight method for
inter-processor communication, typically using hardware mailbox registers or messaging units
to pass data and trigger interrupts on remote processors.

The IPM API provides a generic interface for inter-processor communication. Key features include:

**Message Passing**
  Send messages to remote processors/cores.
  Receive messages from remote processors/cores.
  Interrupt-driven notification of message arrival.

**Channel Support**
  Multiple independent communication channels.
  Channel identification for message routing.

**Callback Mechanism**
  Register callbacks for incoming messages.
  Execute in interrupt context for low-latency response.

**Blocking and Non-blocking Operations**
  Non-blocking send with status checking.
  Maximum message size defined by hardware capabilities.

Devicetree Configuration
************************

IPM devices are defined in the Devicetree with an ``ipm`` or messaging-unit-specific
compatible property. The configuration depends on the hardware vendor and messaging unit type.

Example of an IPM device definition:

.. code-block:: devicetree

   ipm0: ipm@40028000 {
       compatible = "nxp,imx-mu";
       reg = <0x40028000 0x4000>;
       interrupts = <33 0>;
       status = "okay";
   };

Example of referencing an IPM device as an alias:

.. code-block:: devicetree

   aliases {
       ipm0 = &ipm0;
   };

Basic Operation
***************

IPM operations involve registering a callback for incoming messages and
sending messages to remote processors.

.. code-block:: c
   :caption: Configure and use IPM for inter-processor communication

   #include <zephyr/drivers/ipm.h>

   #define IPM_NODE DT_NODELABEL(ipm0)

   static const struct device *ipm_dev = DEVICE_DT_GET(IPM_NODE);

   static void ipm_callback(const struct device *dev, void *user_data,
                            uint32_t id, volatile void *data)
   {
       printk("IPM message received - ID: %u\n", id);
       
       /* Echo the message back */
       uint32_t *msg = (uint32_t *)data;
       ipm_send(dev, 1, id, msg, sizeof(uint32_t));
   }

   void setup_ipm(void)
   {
       int ret;

       if (!device_is_ready(ipm_dev)) {
           printk("IPM device not ready\n");
           return;
       }

       /* Set maximum ID for messages (hardware-dependent) */
       ret = ipm_set_enabled(ipm_dev, 1);
       if (ret < 0) {
           printk("Failed to enable IPM: %d\n", ret);
           return;
       }

       /* Register callback for incoming messages */
       ipm_register_callback(ipm_dev, ipm_callback, NULL);

       /* Send a message to remote core */
       uint32_t message = 0x12345678;
       ret = ipm_send(ipm_dev, 0, 0, &message, sizeof(message));
       if (ret < 0) {
           printk("Failed to send IPM message: %d\n", ret);
       }
   }

Refer to :zephyr:code-sample:`ipm-imx` for a complete example of using the IPM API
on NXP i.MX processors.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_IPM`
* :kconfig:option:`CONFIG_IPM_CONSOLE_SENDER`
* :kconfig:option:`CONFIG_IPM_CONSOLE_RECEIVER`

API Reference
*************

.. doxygengroup:: ipm_interface
