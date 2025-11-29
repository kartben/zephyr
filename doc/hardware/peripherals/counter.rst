.. _counter_api:

Counter
#######

Overview
********

A counter is a hardware peripheral that provides timing services by incrementing or
decrementing a counter value at a fixed frequency. Counters are commonly used for
timing applications, generating periodic events, and measuring time intervals.

The Counter API provides a generic interface for counter/timer peripherals. Key features include:

**Counting Operations**
  Start, stop, and read counter values.
  Support for both count-up and count-down modes.

**Alarms**
  Set alarms to trigger callbacks at specific counter values.
  Configure multiple independent alarm channels.
  Support for absolute and relative alarm values.

**Top Value Configuration**
  Set the maximum counter value (wrap-around point).
  Configure behavior when counter reaches top value.

**Time Conversion**
  Convert between counter ticks and time units (microseconds).

Devicetree Configuration
************************

Counter devices are defined in the Devicetree with a ``counter`` compatible property.
The exact compatible string depends on the hardware vendor and counter type.

Example of a counter device definition:

.. code-block:: devicetree

   counter0: counter@40008000 {
       compatible = "nordic,nrf-timer";
       reg = <0x40008000 0x1000>;
       interrupts = <8 1>;
       status = "okay";
   };

Example of referencing a counter as an alias:

.. code-block:: devicetree

   aliases {
       counter0 = &counter0;
   };

Basic Operation
***************

Counter operations typically involve getting the device, configuring alarms,
and handling alarm callbacks.

.. code-block:: c
   :caption: Configure a counter and set an alarm

   #include <zephyr/drivers/counter.h>

   static void alarm_callback(const struct device *dev, uint8_t chan_id,
                              uint32_t ticks, void *user_data)
   {
       printk("Alarm triggered at %u ticks\n", ticks);
   }

   void setup_counter(void)
   {
       const struct device *counter_dev = DEVICE_DT_GET(DT_ALIAS(counter0));
       struct counter_alarm_cfg alarm_cfg;
       int err;

       if (!device_is_ready(counter_dev)) {
           printk("Counter device not ready\n");
           return;
       }

       /* Start the counter */
       counter_start(counter_dev);

       /* Configure alarm */
       alarm_cfg.flags = 0;
       alarm_cfg.ticks = counter_us_to_ticks(counter_dev, 1000000); /* 1 second */
       alarm_cfg.callback = alarm_callback;
       alarm_cfg.user_data = NULL;

       /* Set the alarm */
       err = counter_set_channel_alarm(counter_dev, 0, &alarm_cfg);
       if (err != 0) {
           printk("Failed to set alarm: %d\n", err);
       }
   }

Refer to :zephyr:code-sample:`alarm` for a complete example of using the Counter API.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_COUNTER`

API Reference
*************

.. doxygengroup:: counter_interface
