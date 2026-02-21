.. _ptp_clock_api:

Precision Time Protocol (PTP) Clock
####################################

Overview
********

The PTP clock driver API provides access to hardware clocks that support the
Precision Time Protocol (IEEE 1588 / PTP). PTP is used in networked systems
where sub-microsecond clock synchronization across nodes is required, such as
in industrial automation, telecommunications, and audio/video bridging.

The API exposes four operations on a PTP clock device:

- :c:func:`ptp_clock_set` — set the current time.
- :c:func:`ptp_clock_get` — read the current time.
- :c:func:`ptp_clock_adjust` — apply a one-shot offset correction (in
  nanoseconds).
- :c:func:`ptp_clock_rate_adjust` — adjust the rate (frequency ratio) of the
  clock relative to its nominal rate.

PTP clock devices are typically accessed through the network stack's
PTP layer rather than directly from application code.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_PTP_CLOCK`

API Reference
*************

.. doxygengroup:: ptp_clock_interface
