.. _counter_api:

Counter
#######

Overview
********

A counter is a hardware block that increments or decrements a register at a fixed rate, usually
derived from a clock through a prescaler, or on each occurrence of an external event. General
purpose timers, real-time counters (RTC) and low-power wake-up timers are all exposed through this
API. Most of them provide compare channels that raise an interrupt when the count matches a
programmed value, and some can latch the current count when an input signal changes.

The counter API abstracts these blocks as devices that can be started, stopped and read, on which
single-shot alarms can be armed per channel, and whose wrap-around value (the top value) can be
changed to obtain periodic callbacks. Optional extensions cover input capture, clock calibration and
counters wider than 32 bits. Key concepts include:

**Ticks and frequency**
  Counter values, alarms and top values are expressed in ticks. :c:func:`counter_get_frequency`
  returns the tick rate in Hz, or zero when the counter counts asynchronous events. Helpers such as
  :c:func:`counter_us_to_ticks` and :c:func:`counter_ticks_to_us` convert between ticks and time.

**Counting direction**
  A counter counts either up from zero to the top value or down from the top value to zero. The
  driver declares the direction with :c:macro:`COUNTER_CONFIG_INFO_COUNT_UP` in
  :c:struct:`counter_config_info`, and :c:func:`counter_is_counting_up` reports it.

**Top value**
  The value at which the counter wraps around. It defaults to :c:func:`counter_get_max_top_value`
  and can be lowered with :c:func:`counter_set_top_value` and a :c:struct:`counter_top_cfg`,
  optionally registering a callback that runs on every wrap.

**Alarm channels**
  Each channel holds at most one single-shot alarm, described by :c:struct:`counter_alarm_cfg` and
  armed with :c:func:`counter_set_channel_alarm`. :c:func:`counter_get_num_of_channels` returns
  the number of channels of a device.

**Guard period**
  A window of ticks, set with :c:func:`counter_set_guard_period`, that lets the driver decide
  whether an absolute alarm value already behind the current count was set too late or is meant
  for the next wrap-around of the counter.

**Capture**
  With :kconfig:option:`CONFIG_COUNTER_CAPTURE` enabled, :c:func:`counter_capture_configure` makes
  a channel record the counter value on an edge of its input and deliver it through a callback.
  Capture channels described in devicetree are retrieved as :c:struct:`counter_capture_dt_spec`.

Typical application flow
========================

#. Get the counter device from devicetree and check it with :c:func:`device_is_ready`.
#. Convert the required delays to ticks with :c:func:`counter_us_to_ticks`; alarm values must not
   exceed the current top value, which itself cannot exceed :c:func:`counter_get_max_top_value`.
#. Start the counter with :c:func:`counter_start`, and optionally set a top value and top callback
   with :c:func:`counter_set_top_value`.
#. Arm alarms with :c:func:`counter_set_channel_alarm`, or configure and enable capture channels.
#. Read the current value with :c:func:`counter_get_value` whenever a timestamp is needed.
#. Cancel pending alarms with :c:func:`counter_cancel_channel_alarm` and stop the counter with
   :c:func:`counter_stop` when it is no longer needed.

Devicetree Configuration
************************

Counter devices are regular devicetree nodes whose properties are defined by the binding of each
driver; there is no generic counter binding. Nordic timers (:dtcompatible:`nordic,nrf-timer`), for
example, declare the number of compare channels, the counter width and the prescaler that derives
the tick rate from the base clock:

.. code-block:: devicetree
   :caption: nRF52840 timer node, as defined in the SoC devicetree

   timer0: timer@40008000 {
       compatible = "nordic,nrf-timer";
       status = "disabled";
       reg = <0x40008000 0x1000>;
       cc-num = <4>;
       max-bit-width = <32>;
       interrupts = <8 NRF_DEFAULT_IRQ_PRIORITY>;
       prescaler = <0>;
   };

Other bindings differ: :dtcompatible:`nordic,nrf-rtc` adds ``fixed-top``, which restricts the
instance to its maximum top value and frees one more channel, :dtcompatible:`st,stm32-counter`
nodes are ``counter`` children of a ``timers`` node that inherit its prescaler, and
:dtcompatible:`arm,cmsdk-timer` only needs ``reg`` and ``interrupts``.

Applications enable the instance they use in an overlay (``&timer0 { status = "okay"; };``) and
select it through a node label, an alias or a ``chosen`` property; the :zephyr:code-sample:`alarm`
sample, for example, uses ``DT_NODELABEL``, ``DT_ALIAS(counter)`` or ``DT_CHOSEN(counter)``
depending on the board. Zephyr itself selects counters through the
``zephyr,system-timer-companion``, ``zephyr,cpu-load-counter`` and ``zephyr,sensor-clock``
:ref:`chosen properties <devicetree-zephyr-chosen-nodes>` for the system timer low-power companion
(:kconfig:option:`CONFIG_SYSTEM_TIMER_LPM_COMPANION_COUNTER`), the :ref:`cpu_load` subsystem and
sensor timestamps.

Capture channels
================

Drivers that support capture declare ``#counter-capture-cells`` on the controller node, with cells
named ``channel`` and ``flags``. Consumers reference a channel through a ``counter-captures``
phandle-array property, optionally named with ``counter-capture-names``, using the edge flags from
:zephyr_file:`include/zephyr/dt-bindings/counter/counter-capture.h`. Routing an input to a channel
is hardware specific: STM32 timers use ``pinctrl`` and :dtcompatible:`espressif,esp32-counter` uses
``capture-gpios``. In the example below the consumer is the
:ref:`zephyr,user node <dt-zephyr-user>`; the STM32H5 SoC devicetree already sets
``#counter-capture-cells = <2>`` on the ``counter`` node.

.. code-block:: devicetree
   :caption: Capture channel 0 of STM32 ``TIM2`` on pin ``PA0``, referenced from ``zephyr,user``

   #include <zephyr/dt-bindings/counter/counter-capture.h>

   &timers2 {
       st,prescaler = <239>;
       status = "okay";

       capture: counter {
           status = "okay";
           pinctrl-0 = <&tim2_ch1_pa0>;
           pinctrl-names = "default";
       };
   };

   / {
       zephyr,user {
           counter-captures = <&capture 0 COUNTER_CAPTURE_RISING_EDGE>;
           counter-capture-names = "pulse";
       };
   };

Basic Operation
***************

:c:func:`counter_start` starts the counter in free-running mode and :c:func:`counter_stop` stops it;
drivers for counters that cannot be stopped return ``-ENOTSUP`` from the latter.
:c:func:`counter_get_value` reads the current tick count, which on a down-counting device must be
subtracted from :c:func:`counter_get_top_value` to obtain the elapsed ticks. :c:func:`counter_reset`
and :c:func:`counter_set_value` are optional and return ``-ENOSYS`` when the driver does not
implement them. :c:func:`counter_us_to_ticks` and :c:func:`counter_ns_to_ticks` saturate at
``UINT32_MAX`` when the result does not fit; the ``_64`` variants such as
:c:func:`counter_us_to_ticks_64` handle longer intervals.

Alarms
======

An alarm is armed on a channel with :c:func:`counter_set_channel_alarm` and a
:c:struct:`counter_alarm_cfg`. :c:member:`counter_alarm_cfg.ticks` is relative to the current count
unless :c:macro:`COUNTER_ALARM_CFG_ABSOLUTE` is set in :c:member:`counter_alarm_cfg.flags`, and in
both cases it must not exceed the current top value. The callback cannot be ``NULL``. Alarms are
single shot: once the callback has run, the channel is free and can be armed again, including from
within the callback itself. :c:func:`counter_cancel_channel_alarm` disarms a pending alarm.

.. code-block:: c
   :caption: Arming a relative alarm and re-arming it from the callback

   static struct counter_alarm_cfg alarm_cfg;

   static void alarm_handler(const struct device *dev, uint8_t chan_id, uint32_t ticks,
                             void *user_data)
   {
       struct counter_alarm_cfg *cfg = user_data;

       /* Interrupt context: double the delay and arm the channel again */
       cfg->ticks *= 2U;
       (void)counter_set_channel_alarm(dev, chan_id, cfg);
   }

   ...

   err = counter_start(counter_dev);
   if (err < 0) {
       return err;
   }

   alarm_cfg.flags = 0;
   alarm_cfg.ticks = counter_us_to_ticks(counter_dev, 2000000);
   alarm_cfg.callback = alarm_handler;
   alarm_cfg.user_data = &alarm_cfg;

   err = counter_set_channel_alarm(counter_dev, 0, &alarm_cfg);

:c:func:`counter_set_channel_alarm` returns ``-ENOTSUP`` for a channel the device does not have,
``-EINVAL`` for invalid settings, ``-EBUSY`` when the channel already holds an alarm and ``-ETIME``
when an absolute alarm was set too late (see :ref:`counter_guard_period`). The
:zephyr:code-sample:`alarm` sample is a complete version of this example.

Top value and periodic callbacks
================================

:c:func:`counter_set_top_value` takes a :c:struct:`counter_top_cfg` with the new top value, an
optional callback that runs on every wrap, and its user data. This is the simplest way to obtain a
periodic interrupt: the ``timer periodic`` shell command described below is implemented this way.
The new value cannot exceed :c:func:`counter_get_max_top_value` (``-EINVAL``) and cannot be changed
while an alarm is active (``-EBUSY``). By default the counter is reset when the top value changes;
:c:macro:`COUNTER_TOP_CFG_DONT_RESET` keeps it running instead, in which case the call fails with
``-ETIME`` if the count is already beyond the new top value, and
:c:macro:`COUNTER_TOP_CFG_RESET_WHEN_LATE` additionally lets the driver reset the counter when that
happens. Drivers that only support their maximum top value, or that cannot honor the reset flags,
return ``-ENOTSUP``.

.. _counter_guard_period:

Guard period and late alarms
============================

When an absolute alarm is requested close to the current count, the counter may pass the target
before the driver has programmed the compare register. The driver cannot tell a target that was
just missed from one intentionally placed after the next wrap-around, and a missed alarm would only
fire after a full counter period. The guard period resolves this ambiguity: an absolute target that
lies less than the guard period behind the current count is considered late.
:c:func:`counter_set_guard_period` sets it, in ticks, with the
:c:macro:`COUNTER_GUARD_PERIOD_LATE_TO_SET` flag. It should cover the worst-case time the driver
needs to activate an alarm and defaults to zero, which disables late detection. Drivers without
this feature return ``-ENOSYS``.

A late alarm makes :c:func:`counter_set_channel_alarm` return ``-ETIME``. The alarm is dropped
unless :c:macro:`COUNTER_ALARM_CFG_EXPIRE_WHEN_LATE` is set, in which case the driver expires it
immediately and still invokes the callback. For an up-counter with a top value of 5000, a guard
period of 100 and a current count of 4950, targets from 4851 to 4950 are late, while 0 to 4850 and
4951 to 4999 are accepted as future values.

Capture
*******

Capture is an experimental extension enabled with :kconfig:option:`CONFIG_COUNTER_CAPTURE`, which is
only available when the selected driver sets :kconfig:option:`CONFIG_COUNTER_SUPPORTS_CAPTURE`. It
timestamps external events in hardware: when the configured edge occurs on a channel input, the
counter value is latched and delivered to a :c:type:`counter_capture_cb_t` callback together with
the channel number and the configured flags.

A channel is configured with :c:func:`counter_capture_configure`, whose
:c:type:`counter_capture_flags_t` argument combines an edge selection
(:c:macro:`COUNTER_CAPTURE_RISING_EDGE`, :c:macro:`COUNTER_CAPTURE_FALLING_EDGE` or
:c:macro:`COUNTER_CAPTURE_BOTH_EDGES`) with :c:macro:`COUNTER_CAPTURE_CONTINUOUS`, which is zero
and reports every matching edge, or :c:macro:`COUNTER_CAPTURE_SINGLE_SHOT`, which reports the first
one only; the upper 24 bits are reserved for SoC-specific flags. Capture is then started with
:c:func:`counter_enable_capture` and stopped with :c:func:`counter_disable_capture`. The ``_dt``
variants take a :c:struct:`counter_capture_dt_spec` obtained with
:c:macro:`COUNTER_CAPTURE_DT_SPEC_GET_BY_NAME`, :c:macro:`COUNTER_CAPTURE_DT_SPEC_GET_BY_IDX` or
:c:macro:`COUNTER_CAPTURE_DT_SPEC_GET` and use the flags from devicetree.

.. code-block:: c
   :caption: Timestamping rising edges on the channel described above

   static const struct counter_capture_dt_spec pulse =
       COUNTER_CAPTURE_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), counter_captures, pulse);

   static void capture_handler(const struct device *dev, uint8_t chan_id,
                               counter_capture_flags_t flags, uint32_t ticks, void *user_data)
   {
       /* ticks is the counter value latched on the edge */
   }

   ...

   err = counter_start(pulse.dev);
   if (err < 0) {
       return err;
   }

   err = counter_capture_configure_dt(&pulse, capture_handler, NULL);
   if (err < 0) {
       return err;
   }

   err = counter_enable_capture_dt(&pulse);

All capture functions return ``-ENOTSUP`` when the driver does not implement capture or the channel
does not exist. :zephyr_file:`tests/drivers/counter/counter_capture` contains board overlays and a
test of every edge and mode combination.

64-bit ticks and calibration
****************************

Counters wider than 32 bits are supported when the driver sets
:kconfig:option:`CONFIG_COUNTER_SUPPORTS_64BITS_TICKS` and the application enables
:kconfig:option:`CONFIG_COUNTER_64BITS_TICKS`. The ``_64`` variants of the value, alarm, top value,
guard period and capture functions, such as :c:func:`counter_get_value_64` and
:c:func:`counter_set_channel_alarm_64` with :c:struct:`counter_alarm_cfg_64`, then operate on the
full width, while :c:func:`counter_get_max_top_value` only returns the lower 32 bits of the maximum.
When the option is disabled, :c:func:`counter_get_value_64`, :c:func:`counter_set_value_64`,
:c:func:`counter_set_channel_alarm_64`, :c:func:`counter_set_top_value_64` and
:c:func:`counter_set_guard_period_64` return ``-ENOTSUP``, :c:func:`counter_get_top_value_64`
returns zero, and the remaining ``_64`` getters fall back to their 32-bit counterparts. Drivers
with tick rates above ``UINT32_MAX`` Hz select
:kconfig:option:`CONFIG_COUNTER_64BITS_FREQ`; :c:func:`counter_get_frequency` then saturates at
``UINT32_MAX`` and :c:func:`counter_get_frequency_64` returns the exact rate.

:kconfig:option:`CONFIG_COUNTER_CALIBRATION` adds :c:func:`counter_set_calibration` and
:c:func:`counter_get_calibration`, which trim the counter clock in parts per billion: a positive
value speeds the counter up and a negative value slows it down. Drivers that do not implement
calibration return ``-ENOSYS``, and out-of-range values are rejected with ``-EINVAL``.

Usage constraints
*****************

* Alarm, top value and capture callbacks are invoked from the driver's interrupt handler, except in
  drivers for external RTCs such as the MCP7940N that defer them to the system work queue; in all
  cases they must be short and may only use ISR-safe kernel APIs such as :c:func:`k_sem_give`. On
  Nordic devices the ``zli`` devicetree property moves the handler to a
  :ref:`zero-latency interrupt <zlis>`, where kernel APIs must not be used at all.
* :c:func:`counter_set_channel_alarm` and :c:func:`counter_cancel_channel_alarm` are not thread
  safe; applications that share a channel between threads must serialize access themselves.
* Optional operations report ``-ENOSYS`` when a driver does not implement them, while requests the
  hardware cannot satisfy, such as an unknown channel or an unsupported stop or capture request,
  report ``-ENOTSUP``. Both mean "not supported" rather than a transient failure.
* The counter API functions are system calls usable from user mode threads that have been granted
  access to the device (see :ref:`usermode_api`), with the exception of
  :c:func:`counter_capture_configure` and its ``_64`` and ``_dt`` variants, which are plain inline
  functions.
* Several counter drivers implement device power management; when
  :ref:`runtime power management <pm-device-runtime>` is enabled for such a device, it must be
  requested with :c:func:`pm_device_runtime_get` before use. After a low-power state,
  :c:func:`counter_get_pending_int` reports whether a counter interrupt is pending, which identifies
  the wake-up source.

Shell commands
**************

When :kconfig:option:`CONFIG_COUNTER_SHELL` is enabled, a ``timer`` command is registered by
:zephyr_file:`drivers/counter/counter_timer_shell.c`. Each subcommand takes the name of the counter
device as its first argument; tab completion lists the devices that implement the counter API.
Delays are given in microseconds and are rejected when, converted to ticks, they exceed the
maximum top value.

``timer freerun <timer_instance_node_id>``
  Start the counter in free-running mode with :c:func:`counter_start`.

``timer stop <timer_instance_node_id>``
  Stop the counter with :c:func:`counter_stop`.

``timer oneshot <timer_instance_node_id> <channel_id> <time_in_us>``
  Start the counter if needed, arm a relative alarm on ``channel_id`` (0 to 255) that expires after
  ``time_in_us`` and block until the alarm callback has run.

``timer periodic <timer_instance_node_id> <time_in_us>``
  Start the counter if needed, set the top value so that the counter wraps every ``time_in_us`` and
  block until the top callback has run 10 times. The counter keeps running afterwards.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_COUNTER`
* :kconfig:option:`CONFIG_COUNTER_INIT_PRIORITY`
* :kconfig:option:`CONFIG_COUNTER_SHELL`
* :kconfig:option:`CONFIG_COUNTER_CAPTURE`
* :kconfig:option:`CONFIG_COUNTER_64BITS_TICKS`
* :kconfig:option:`CONFIG_COUNTER_CALIBRATION`

API Reference
*************

.. doxygengroup:: counter_interface

Capture API
===========

.. doxygengroup:: counter_capture
