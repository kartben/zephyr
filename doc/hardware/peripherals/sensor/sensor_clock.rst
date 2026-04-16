.. _sensor-clock:

Sensor Clock
############

Overview
********

The sensor clock feature provides a common time base for sensor drivers and applications that need
timestamps for sensor data.

It is primarily used by sensor drivers that produce timestamped data for the
:ref:`sensor-read-and-decode` APIs and streaming paths.

Drivers can read the current sensor clock cycle count with :c:func:`sensor_clock_get_cycles` and
it to nanoseconds as needed with :c:func:`sensor_clock_cycles_to_ns`.

The converted value is expressed in the selected sensor clock domain. It is a monotonic timestamp
source for sensor data, not a wall-clock time API.

Configuration
*************

:kconfig:option:`CONFIG_SENSOR_CLOCK` enables the sensor clock helpers. It defaults to enabled when
:kconfig:option:`CONFIG_SENSOR_ASYNC_API` is enabled.

With ``CONFIG_SENSOR_CLOCK`` enabled, Kconfig presents the **Sensor clock type** choice (menuconfig
prompt). You must select exactly one backend—not three independent options:

* :kconfig:option:`CONFIG_SENSOR_CLOCK_SYSTEM` (default)
* :kconfig:option:`CONFIG_SENSOR_CLOCK_COUNTER`
* :kconfig:option:`CONFIG_SENSOR_CLOCK_RTC`

**System clock:** If :kconfig:option:`CONFIG_SENSOR_CLOCK_SYSTEM` is selected, timestamps come from
the kernel cycle counter. No ``zephyr,sensor-clock`` chosen node is used or required.

**Device-backed clock:** :kconfig:option:`CONFIG_SENSOR_CLOCK_COUNTER` and
:kconfig:option:`CONFIG_SENSOR_CLOCK_RTC` use the same implementation: the sensor clock reads the
Counter driver for the device assigned by the ``zephyr,sensor-clock`` chosen node. For example:

.. code-block:: dts

   / {
       chosen {
           zephyr,sensor-clock = &timer0;
       };
   };

The chosen node must reference a counter device with a known, fixed frequency that counts up
monotonically. Selecting Counter or RTC without defining ``zephyr,sensor-clock`` fails the build at
configure time (CMake ``FATAL_ERROR``).

Usage
*****

Applications usually consume sensor timestamps from decoded sensor data rather
than calling the sensor clock helpers directly. The helper API is also
available to applications and drivers that need direct access to the selected
sensor clock source.

See the :zephyr:code-sample:`sensor_clock` sample for a minimal example that
reads the current sensor clock and prints both cycles and nanoseconds.

API Reference
*************

.. doxygengroup:: sensor_clock_interface
