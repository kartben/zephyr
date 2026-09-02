.. _tgpio_api:

Time-aware General-Purpose Input/Output (TGPIO)
###############################################

Overview
********

A time-aware GPIO (TGPIO) controller couples a set of GPIO pins to a free-running hardware
timer. Instead of driving a pin or sampling it under software control, the controller acts on the
timer value itself: an output pin can be told to emit a pulse when the timer reaches a given
value, and an input pin can capture the exact timer value at which an external edge arrived. This
makes TGPIO suitable for clock synchronization, precise event timestamping and the generation of
periodic reference signals whose timing does not depend on software latency.

The Zephyr TGPIO API abstracts such controllers as a device exposing a timer and a set of
numbered pins. All times and intervals are expressed in cycles of the controller's own timer;
the API provides the running rate so the application can convert between cycles and seconds.
The only in-tree implementation is the Intel driver (:dtcompatible:`intel,timeaware-gpio`), where
the timer is the Always Running Timer (ART) of the Intel PCH. Key concepts include:

**Timer**
  The reference clock of the controller. :c:func:`tgpio_port_get_time` returns its current
  64-bit value in cycles and :c:func:`tgpio_port_get_cycles_per_second` returns its frequency.
  All start times, intervals and captured timestamps refer to this timer.

**Pins**
  A pin is identified by a zero-based index local to the controller. A pin is used either as a
  scheduled output or as a timestamped input; the two roles are selected by the function used to
  configure it. :c:func:`tgpio_pin_disable` stops whatever operation is running on a pin.

**Scheduled and periodic output**
  :c:func:`tgpio_pin_periodic_output` arms an output pin to produce a pulse when the timer reaches
  a start time and, optionally, to repeat it at a fixed interval.

**Timestamped input**
  :c:func:`tgpio_pin_config_ext_timestamp` enables event capture on an input pin for the edge
  selected with :c:enum:`tgpio_pin_polarity`. :c:func:`tgpio_pin_read_ts_ec` then returns the
  timestamp of the last captured event together with a running event counter.

**Driver interface**
  Drivers implement :c:struct:`tgpio_driver_api`. All operations are mandatory and every public
  function is a system call, so the API can be used from user mode threads.

Timer and time base
*******************

The controller timer is the single time base for all TGPIO operations. Applications obtain the
current value with :c:func:`tgpio_port_get_time` and the frequency in Hz with
:c:func:`tgpio_port_get_cycles_per_second`. A duration in seconds is converted to a number of
cycles by multiplying it by the frequency; for example, an interval of one second corresponds to
``cycles`` timer ticks.

Start times passed to :c:func:`tgpio_pin_periodic_output` are absolute timer values, not offsets
from the current time. The application therefore reads the current time first and adds the
desired delay to it. A start time that is already in the past is not rejected by the API; whether
and when the hardware acts on it is controller specific, so the application must make sure the
start time lies in the future.

In the Intel driver, the frequency is a fixed value taken from devicetree, and reading the timer
may return ``-ETIMEDOUT`` on platforms that require the ART capture handshake if the hardware does
not complete it in time.

Scheduled and periodic output
*****************************

:c:func:`tgpio_pin_periodic_output` takes the pin index, the absolute start time of the first
pulse, the repeat interval between two pulses and a ``periodic_enable`` flag. When the flag is
``true``, the pin emits a pulse at ``start_time`` and then every ``repeat_interval`` cycles until
it is disabled. When the flag is ``false``, periodic mode is left off and only the first pulse is
scheduled.

Calling the function reconfigures the pin from scratch: the Intel driver first disables the pin,
then programs the interval and comparator registers and finally enables the pin as an output in
one step, so a previous configuration on the same pin is replaced rather than extended.

The output runs entirely in hardware. No interrupt or callback is delivered to the application
when a pulse is generated; :c:func:`tgpio_pin_disable` stops the generation.

Timestamped input
*****************

:c:func:`tgpio_pin_config_ext_timestamp` configures a pin as an input and starts capturing
events. The ``event_polarity`` argument selects which edges are captured and takes one of the
values of :c:enum:`tgpio_pin_polarity`:

* :c:enumerator:`TGPIO_RISING_EDGE`: rising edges only,
* :c:enumerator:`TGPIO_FALLING_EDGE`: falling edges only,
* :c:enumerator:`TGPIO_TOGGLE_EDGE`: both edges.

Once the pin is enabled, the hardware records the timer value of every matching edge and counts
the events. :c:func:`tgpio_pin_read_ts_ec` reads both values: ``timestamp`` is the timer value at
the most recent event and ``event_count`` is the number of events captured since the pin was
enabled. The values are read on demand; the API does not notify the application when an event
arrives, so an application that must not miss events polls the event counter at a rate higher
than the expected event rate and detects new events by comparing successive counter values.

Devicetree Configuration
************************

A TGPIO controller is described by a single devicetree node. The Intel binding requires the
register block, the timer frequency and the number of pins; an optional ``artv-ctrl`` flag marks
platforms on which reading the timer requires a capture handshake. The following node is taken
from the Raptor Lake S SoC description:

.. code-block:: devicetree

   tgpio: tgpio@fe001200 {
       compatible = "intel,timeaware-gpio";
       reg = <0xfe001200 0x100>;
       timer-clock = <19200000>;
       max-pins = <2>;
       status = "okay";
   };

Here the timer runs at 19.2 MHz and two pins, ``0`` and ``1``, are available. The driver uses
``max-pins`` to validate the pin index passed to every pin operation and returns ``-EINVAL`` for
indexes outside the range. Boards enable the controller by setting the node status in their DTS
or in an application overlay:

.. code-block:: devicetree

   &tgpio {
       status = "okay";
   };

Typical application flow
************************

Typical use of the TGPIO API is:

#. Get the TGPIO device from devicetree with :c:macro:`DEVICE_DT_GET` and check it with
   :c:func:`device_is_ready`.
#. Read the current timer value with :c:func:`tgpio_port_get_time` and the timer frequency with
   :c:func:`tgpio_port_get_cycles_per_second`.
#. Compute the absolute start time of the first output pulse by adding the desired delay in cycles
   to the current time, then call :c:func:`tgpio_pin_periodic_output` on the output pin.
#. Enable event capture on the input pin with :c:func:`tgpio_pin_config_ext_timestamp`.
#. Periodically read the last timestamp and the event counter with
   :c:func:`tgpio_pin_read_ts_ec`.
#. Stop either operation with :c:func:`tgpio_pin_disable` when it is no longer needed.

Basic Operation
***************

The following example generates a 1 Hz pulse train on pin ``1``, starting one second from now,
and timestamps the rising edges seen on pin ``0``. With the two pins looped back, each timestamp
matches one of the scheduled pulses. It is derived from the :zephyr:code-sample:`timeaware-gpio`
sample.

.. code-block:: c
   :caption: Periodic output on one pin and event timestamping on another

   #include <zephyr/drivers/timeaware_gpio.h>

   #define TGPIO_PIN_IN  0
   #define TGPIO_PIN_OUT 1

   const struct device *tgpio_dev = DEVICE_DT_GET(DT_NODELABEL(tgpio));
   uint64_t now, ts, ec;
   uint32_t cycles;
   int ret;

   if (!device_is_ready(tgpio_dev)) {
       return -ENODEV;
   }

   ret = tgpio_port_get_time(tgpio_dev, &now);
   if (ret < 0) {
       return ret;
   }

   ret = tgpio_port_get_cycles_per_second(tgpio_dev, &cycles);
   if (ret < 0) {
       return ret;
   }

   /* First pulse one second from now, then one pulse per second */
   ret = tgpio_pin_periodic_output(tgpio_dev, TGPIO_PIN_OUT, now + cycles, cycles, true);
   if (ret < 0) {
       return ret;
   }

   ret = tgpio_pin_config_ext_timestamp(tgpio_dev, TGPIO_PIN_IN, TGPIO_RISING_EDGE);
   if (ret < 0) {
       return ret;
   }

   while (1) {
       ret = tgpio_pin_read_ts_ec(tgpio_dev, TGPIO_PIN_IN, &ts, &ec);
       if (ret < 0) {
           return ret;
       }
       printk("timestamp: %016llx, event count: %llu\n", ts, ec);
       k_sleep(K_MSEC(500));
   }

Usage constraints
*****************

* The API is polling based. There are no callbacks, interrupts or blocking waits: output pulses
  are generated by hardware without software involvement and input events are only observed when
  the application calls :c:func:`tgpio_pin_read_ts_ec`.
* All functions are system calls and validate the device and, where applicable, the output
  pointers, so they can be called from user mode threads. The Intel driver does not sleep and does
  not take locks; concurrent configuration of the same pin from several threads is not serialized
  by the API and must be avoided by the application.
* All functions return ``0`` on success and a negative errno value on failure. In the Intel
  driver, an invalid pin index results in ``-EINVAL``.
* The API does not define power management hooks; the Intel driver maps its register block at
  initialization and keeps it active.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_TIMEAWARE_GPIO`
* :kconfig:option:`CONFIG_TIMEAWARE_GPIO_INIT_PRIORITY`
* :kconfig:option:`CONFIG_TIMEAWARE_GPIO_INTEL`

API Reference
*************

.. doxygengroup:: tgpio_interface
