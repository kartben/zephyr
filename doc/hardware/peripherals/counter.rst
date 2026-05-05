.. _counter_api:

Counter
#######

Overview
********

The counter driver API provides access to hardware timers and counters. It
supports:

- Counting up or down with a configurable top (wrap) value.
- One-shot and periodic alarms triggered at a future count value.
- Reading the current count and checking whether the counter is running.

A counter device may expose multiple independent alarm channels, each able to
fire a callback at a specified tick count. The maximum number of channels and
the counting frequency are hardware-dependent.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_COUNTER`

API Reference
*************

.. doxygengroup:: counter_interface
