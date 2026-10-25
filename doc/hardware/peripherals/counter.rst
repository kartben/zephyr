.. _counter_api:

Counter
#######

Overview
********

A counter is a hardware peripheral that provides timing and counting capabilities.
It can be used to generate periodic interrupts (alarms), measure time intervals, and count
events. Counters are commonly implemented using hardware timers or Real-Time Clocks (RTCs).

The Counter API provides a generic method to interact with counter devices. It allows
applications to configure counters, set alarms, read counter values, and manage interrupts.
Key features include:

**Basic Counter Operations**
  Start and stop counters, read current counter values, and reset counters to their
  initial state. Support for both 32-bit and 64-bit counter values.

**Alarm Configuration**
  Set single-shot alarms at specific tick values (absolute or relative) with callbacks
  that execute when the alarm expires. Multiple alarm channels may be supported depending
  on the hardware.

**Top Value Configuration**
  Configure the maximum counter value (top value) at which the counter wraps around.
  The counter can be reset to zero or the top value depending on the counting direction.
  Optional callbacks can be registered to execute on each counter wraparound.

**Guard Period**
  Configure guard periods to detect when absolute alarms are set too close to the current
  counter value, preventing unintended wraparound behavior.

**Time Conversion Utilities**
  Convert between counter ticks and time units (microseconds, nanoseconds) for counters
  that run at a fixed frequency.

**Counting Direction**
  Counters can count up (incrementing) or down (decrementing) depending on the hardware
  implementation.

Devicetree Configuration
************************

Counter devices are defined in the Devicetree and can be referenced by applications and
drivers. Each counter device has properties that describe its capabilities such as frequency,
maximum top value, and the number of alarm channels.

Example of a counter device definition:

.. code-block:: devicetree

   rtc0: rtc@4000b000 {
       compatible = "nordic,nrf-rtc";
       reg = <0x4000b000 0x1000>;
       interrupts = <11 1>;
       prescaler = <1>;
   };

Example of referencing a counter device by node label:

.. code-block:: devicetree

   / {
       aliases {
           counter0 = &rtc0;
       };
   };

Or by using the ``zephyr,counter`` chosen node:

.. code-block:: devicetree

   / {
       chosen {
           zephyr,counter = &rtc0;
       };
   };

Basic Operation
***************

Counter operations are performed on a device pointer obtained from the Devicetree.
The most common approach is to use :c:macro:`DEVICE_DT_GET` to get a reference to the
counter device.

.. code-block:: c
   :caption: Getting a reference to a counter device defined as alias ``counter0``

   #define COUNTER_NODE DT_ALIAS(counter0)
   const struct device *counter_dev = DEVICE_DT_GET(COUNTER_NODE);

Before using a counter device, verify it is ready:

.. code-block:: c
   :caption: Checking if a counter device is ready

   if (!device_is_ready(counter_dev)) {
       printk("Counter device not ready\n");
       return -ENODEV;
   }

Starting and Reading the Counter
=================================

Start the counter and read its current value:

.. code-block:: c
   :caption: Start a counter and read its current value

   int ret;
   uint32_t ticks;

   /* Start the counter in free-running mode */
   ret = counter_start(counter_dev);
   if (ret < 0) {
       printk("Failed to start counter (err %d)\n", ret);
       return ret;
   }

   /* Read the current counter value */
   ret = counter_get_value(counter_dev, &ticks);
   if (ret < 0) {
       printk("Failed to read counter value (err %d)\n", ret);
       return ret;
   }

   printk("Counter value: %u ticks\n", ticks);

For counters that support 64-bit values, use :c:func:`counter_get_value_64`:

.. code-block:: c

   uint64_t ticks_64;
   ret = counter_get_value_64(counter_dev, &ticks_64);

Alarm Configuration
*******************

Counters can trigger alarms at specific tick values. Alarms can be configured as either
relative (offset from current value) or absolute (specific tick value).

Setting a Relative Alarm
=========================

A relative alarm expires after a specified number of ticks from the current counter value:

.. code-block:: c
   :caption: Set a relative alarm that expires after 1 second

   #define ALARM_CHANNEL_ID 0
   #define DELAY_US 1000000

   static void alarm_callback(const struct device *dev,
                             uint8_t chan_id, uint32_t ticks,
                             void *user_data)
   {
       printk("Alarm expired at %u ticks\n", ticks);
   }

   struct counter_alarm_cfg alarm_cfg = {
       .flags = 0,  /* Relative alarm */
       .ticks = counter_us_to_ticks(counter_dev, DELAY_US),
       .callback = alarm_callback,
       .user_data = NULL
   };

   int ret = counter_set_channel_alarm(counter_dev, ALARM_CHANNEL_ID, &alarm_cfg);
   if (ret < 0) {
       printk("Failed to set alarm (err %d)\n", ret);
   }

Setting an Absolute Alarm
==========================

An absolute alarm expires when the counter reaches a specific tick value:

.. code-block:: c
   :caption: Set an absolute alarm at a specific tick value

   uint32_t current_ticks;
   counter_get_value(counter_dev, &current_ticks);

   struct counter_alarm_cfg alarm_cfg = {
       .flags = COUNTER_ALARM_CFG_ABSOLUTE,  /* Absolute alarm */
       .ticks = current_ticks + counter_us_to_ticks(counter_dev, DELAY_US),
       .callback = alarm_callback,
       .user_data = NULL
   };

   int ret = counter_set_channel_alarm(counter_dev, ALARM_CHANNEL_ID, &alarm_cfg);
   if (ret == -ETIME) {
       printk("Alarm time already passed\n");
   }

When setting absolute alarms close to the current counter value, there's a risk that the
counter may have already passed the target value. Use the guard period feature to handle
this situation (see :ref:`counter_guard_period`).

Canceling Alarms
================

Active alarms can be canceled before they expire:

.. code-block:: c

   int ret = counter_cancel_channel_alarm(counter_dev, ALARM_CHANNEL_ID);
   if (ret < 0) {
       printk("Failed to cancel alarm (err %d)\n", ret);
   }

Refer to :zephyr:code-sample:`alarm` for a complete example using counter alarms.

Top Value Configuration
***********************

The top value determines when the counter wraps around. By default, counters use their
maximum possible top value (e.g., 2^32-1 for 32-bit counters), but this can be changed
to create counters with specific periods.

Setting a Custom Top Value
===========================

.. code-block:: c
   :caption: Configure counter to wrap at 1000 ticks

   static void top_callback(const struct device *dev, void *user_data)
   {
       printk("Counter wrapped around\n");
   }

   struct counter_top_cfg top_cfg = {
       .ticks = 1000,
       .callback = top_callback,
       .user_data = NULL,
       .flags = 0
   };

   int ret = counter_set_top_value(counter_dev, &top_cfg);
   if (ret < 0) {
       printk("Failed to set top value (err %d)\n", ret);
   }

The :c:macro:`COUNTER_TOP_CFG_DONT_RESET` flag can be used to update the top value
without resetting the counter. However, if the new top value is less than the current
counter value, an error will be returned unless :c:macro:`COUNTER_TOP_CFG_RESET_WHEN_LATE`
is also set.

.. code-block:: c
   :caption: Update top value without resetting the counter

   struct counter_top_cfg top_cfg = {
       .ticks = 2000,
       .callback = NULL,
       .user_data = NULL,
       .flags = COUNTER_TOP_CFG_DONT_RESET | COUNTER_TOP_CFG_RESET_WHEN_LATE
   };

   int ret = counter_set_top_value(counter_dev, &top_cfg);

.. _counter_guard_period:

Guard Period
************

When setting absolute alarms, there's a risk that the counter advances past the target
tick value before the alarm is activated. The guard period helps detect this condition.

The guard period is specified in counter ticks and represents the minimum safe distance
between the current counter value and the alarm target. If the target falls within the
guard period:

- With :c:macro:`COUNTER_ALARM_CFG_EXPIRE_WHEN_LATE`: The alarm expires immediately
- Without this flag: The alarm fails with ``-ETIME``

Setting the Guard Period
=========================

.. code-block:: c
   :caption: Set guard period to 100 ticks

   uint32_t guard_ticks = 100;
   int ret = counter_set_guard_period(counter_dev, guard_ticks,
                                      COUNTER_GUARD_PERIOD_LATE_TO_SET);
   if (ret < 0) {
       printk("Failed to set guard period (err %d)\n", ret);
   }

For short alarm periods, setting a large guard period (e.g., half the counter top value)
can prevent unintended wraparound behavior.

Time Conversion Utilities
**************************

For counters that run at a fixed frequency, the API provides utilities to convert between
counter ticks and time units.

Converting Time to Ticks
=========================

.. code-block:: c

   /* Convert 500 milliseconds to ticks */
   uint32_t ticks = counter_us_to_ticks(counter_dev, 500 * USEC_PER_MSEC);

Converting Ticks to Time
=========================

.. code-block:: c

   /* Convert ticks to microseconds */
   uint64_t usec = counter_ticks_to_us(counter_dev, ticks);

   /* Convert ticks to nanoseconds */
   uint64_t nsec = counter_ticks_to_ns(counter_dev, ticks);

Getting Counter Frequency
==========================

.. code-block:: c

   uint32_t freq = counter_get_frequency(counter_dev);
   printk("Counter frequency: %u Hz\n", freq);

Note that some counters may not have a fixed frequency (e.g., counters driven by external
events), in which case :c:func:`counter_get_frequency` returns 0.

Configuration Options
*********************

Main configuration options:

* :kconfig:option:`CONFIG_COUNTER` - Enable counter driver support
* :kconfig:option:`CONFIG_COUNTER_SHELL` - Enable counter shell commands for testing
  and debugging

Specific counter drivers may have additional configuration options. Refer to the driver
documentation for details.

API Reference
*************

.. doxygengroup:: counter_interface
