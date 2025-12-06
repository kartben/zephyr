.. _pwm_api:

Pulse Width Modulation (PWM)
############################

Overview
********

Pulse Width Modulation (PWM) is a method of generating a signal with a variable duty cycle
by modulating the width of pulses. PWM is commonly used for controlling motors, LEDs, servos,
and other devices where analog-like control is needed using digital signals.

The PWM API provides a generic method to interact with Pulse Width Modulation (PWM) controllers.
It allows applications to configure PWM channels, set period and pulse width, and capture
external PWM signals. Key features include:

**Signal Generation**
  Generate PWM signals with configurable period and pulse width (duty cycle).
  Support for normal and inverted polarity.
  Multiple channels per controller for independent signal generation.

**Duty Cycle Control**
  Set duty cycle from 0% (always inactive) to 100% (always active).
  Precise control using nanosecond time units or hardware clock cycles.

**PWM Capture**
  Capture and measure external PWM signals (period and pulse width).
  Single-shot or continuous capture modes.
  Interrupt-driven capture with callback support.

**Devicetree Integration**
  PWM controllers and consumers are defined in the Devicetree, allowing
  drivers and applications to reference them in a hardware-agnostic way using
  :c:struct:`pwm_dt_spec`.

Devicetree Configuration
************************

PWM controllers are defined in the Devicetree as nodes with the ``pwm-controller`` property.
The ``#pwm-cells`` property typically specifies that 3 cells are used to describe a PWM output:
the channel number, the period in nanoseconds, and flags (such as polarity).

PWM Channels
============

A PWM controller typically supports multiple independent output channels. Each channel can
generate a PWM signal with its own duty cycle, though channels often share the same period
(frequency) due to hardware constraints. For example, a 4-channel PWM controller might have
channels 0-3, each capable of driving a separate output pin. When referencing a PWM channel
in the Devicetree, you specify both the controller and the channel number.

Example of a PWM controller definition:

.. code-block:: devicetree

   pwm0: pwm@40002000 {
       compatible = "nordic,nrf-pwm";
       reg = <0x40002000 0x1000>;
       interrupts = <8 1>;
       status = "okay";
       #pwm-cells = <3>;
   };

Example of referencing a PWM output:

.. code-block:: devicetree

   pwm_led0: pwm_led_0 {
       compatible = "pwm-leds";
       pwm_led {
           pwms = <&pwm0 0 PWM_MSEC(20) PWM_POLARITY_NORMAL>;
       };
   };

In this example:

- ``&pwm0`` is a reference to the PWM controller.
- ``0`` is the channel number.
- ``PWM_MSEC(20)`` is the period (20 milliseconds).
- ``PWM_POLARITY_NORMAL`` indicates normal polarity (active-high).

Basic Operation
***************

PWM operations are usually performed on a :c:struct:`pwm_dt_spec` structure, which is a container
for the PWM channel information specified in the Devicetree. This structure is typically populated
using :c:macro:`PWM_DT_SPEC_GET` macro (or any of its variants), then used with functions like
:c:func:`pwm_set_dt` to control the PWM signal.

PWM operations can also be performed directly on a PWM controller device using the channel number
with functions like :c:func:`pwm_set`, which takes a device pointer and channel number as arguments.

For complete examples of PWM signal generation, including LED control and varying duty cycles,
see :zephyr:code-sample:`blinky_pwm`.

Period and Pulse Width Units
=============================

The PWM API uses nanoseconds as the standard unit for specifying period and pulse width. However,
convenience macros are provided to convert from other time units:

- :c:macro:`PWM_NSEC(x)` - Nanoseconds
- :c:macro:`PWM_USEC(x)` - Microseconds
- :c:macro:`PWM_MSEC(x)` - Milliseconds
- :c:macro:`PWM_SEC(x)` - Seconds
- :c:macro:`PWM_HZ(x)` - Frequency in Hertz (converts to period)
- :c:macro:`PWM_KHZ(x)` - Frequency in Kilohertz

These macros can be used both in Devicetree and in C code.

.. code-block:: c
   :caption: Using period helper macros

   /* 1 kHz PWM signal (1ms period) with 25% duty cycle */
   pwm_set_dt(&pwm_led, PWM_KHZ(1), PWM_USEC(250));

   /* 50 Hz PWM signal (20ms period) with 75% duty cycle */
   pwm_set_dt(&pwm_led, PWM_HZ(50), PWM_MSEC(15));

PWM Polarity
============

PWM signals can have normal or inverted polarity:

- :c:macro:`PWM_POLARITY_NORMAL` - Active-high: signal is high during the pulse width and low for the rest of the period.
- :c:macro:`PWM_POLARITY_INVERTED` - Active-low: signal is low during the pulse width and high for the rest of the period.

The polarity is typically specified in the Devicetree but can also be passed as a flag to PWM API functions.

.. code-block:: c
   :caption: Setting PWM with inverted polarity

   pwm_set(pwm_dev, 0, PWM_MSEC(20), PWM_MSEC(10), PWM_POLARITY_INVERTED);

PWM Capture
***********

PWM capture allows measuring the period and pulse width of an external PWM signal. This feature
is useful for decoding PWM-based sensors, measuring external signals, or implementing PWM input
functionality.

.. note::
   PWM capture support must be enabled by selecting :kconfig:option:`CONFIG_PWM_CAPTURE`.

Capture can be performed in two modes:

- **Single-shot capture**: Captures a single period and/or pulse width, then stops.
- **Continuous capture**: Continuously captures PWM signals and invokes a callback for each capture.

The capture API includes functions for blocking capture (:c:func:`pwm_capture_cycles`,
:c:func:`pwm_capture_usec`, :c:func:`pwm_capture_nsec`) as well as non-blocking capture with
callbacks (:c:func:`pwm_configure_capture`, :c:func:`pwm_enable_capture`,
:c:func:`pwm_disable_capture`).

For a complete example demonstrating both single-shot and continuous PWM capture,
see :zephyr:code-sample:`capture`.

Configuration Options
*********************

Main configuration options:

* :kconfig:option:`CONFIG_PWM` - Enable PWM driver support
* :kconfig:option:`CONFIG_PWM_SHELL` - Enable PWM shell commands for testing
* :kconfig:option:`CONFIG_PWM_CAPTURE` - Enable PWM capture functionality
* :kconfig:option:`CONFIG_PWM_INIT_PRIORITY` - PWM driver initialization priority

API Reference
*************

.. doxygengroup:: pwm_interface
