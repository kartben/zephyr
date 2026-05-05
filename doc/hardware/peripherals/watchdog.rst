.. _watchdog_api:

Watchdog
########

Overview
********

The watchdog driver API provides access to hardware watchdog timers. A
watchdog timer is a peripheral that resets the system (or generates an
interrupt) if the application fails to periodically feed (reset) it within a
configured timeout window.

The typical usage pattern is:

1. Install a timeout configuration with :c:func:`wdt_install_timeout`.
2. Start the watchdog with :c:func:`wdt_setup`.
3. Periodically call :c:func:`wdt_feed` to prevent the timeout from expiring.

An optional callback can be registered to run just before the watchdog reset
fires, allowing the application to log diagnostics or perform an orderly
shutdown.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_WATCHDOG`

API Reference
*************

.. doxygengroup:: watchdog_interface
