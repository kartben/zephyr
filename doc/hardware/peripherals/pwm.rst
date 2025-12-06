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
           label = "PWM LED 0";
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
for the PWM channel information specified in the Devicetree.

This structure is typically populated using :c:macro:`PWM_DT_SPEC_GET` macro (or any of its
variants).

.. code-block:: c
   :caption: Populating a pwm_dt_spec structure for a PWM output defined as alias ``pwm_led0``

   #define PWM_LED0_NODE DT_ALIAS(pwm_led0)
   static const struct pwm_dt_spec pwm_led = PWM_DT_SPEC_GET(PWM_LED0_NODE);

The :c:struct:`pwm_dt_spec` structure can then be used to perform PWM operations.

.. code-block:: c
   :caption: Check if PWM device is ready and set a 50% duty cycle

   int ret;

   if (!pwm_is_ready_dt(&pwm_led)) {
       printk("Error: PWM device is not ready\n");
       return -ENODEV;
   }

   /* Set period to 20ms (50Hz) with 50% duty cycle */
   ret = pwm_set_dt(&pwm_led, PWM_MSEC(20), PWM_MSEC(10));
   if (ret < 0) {
       printk("Error: failed to set PWM\n");
       return ret;
   }

.. code-block:: c
   :caption: Fade an LED by varying the pulse width

   uint32_t period = PWM_MSEC(20);
   uint32_t pulse_width = 0;
   
   /* Gradually increase brightness */
   for (int i = 0; i <= 100; i++) {
       pulse_width = (period * i) / 100;
       pwm_set_dt(&pwm_led, period, pulse_width);
       k_msleep(20);
   }

PWM operations can also be performed directly on a PWM controller device using the channel number.
In this case, you will use the PWM API functions that take a device pointer as an argument, such as
:c:func:`pwm_set` instead of :c:func:`pwm_set_dt`.

.. code-block:: c
   :caption: Direct device operation

   const struct device *pwm_dev = DEVICE_DT_GET(DT_NODELABEL(pwm0));
   uint32_t period = PWM_MSEC(20);
   uint32_t pulse = PWM_MSEC(10);
   
   if (!device_is_ready(pwm_dev)) {
       return -ENODEV;
   }
   
   /* Set PWM on channel 0 */
   pwm_set(pwm_dev, 0, period, pulse, PWM_POLARITY_NORMAL);

Refer to :zephyr:code-sample:`blinky_pwm` for a complete example of basic PWM operations using the
:c:struct:`pwm_dt_spec` structure.

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

Simple Capture Example
=======================

The simplest way to capture a PWM signal is using the blocking capture functions:

.. code-block:: c
   :caption: Capture PWM signal period and pulse width

   const struct device *pwm_dev = DEVICE_DT_GET(DT_NODELABEL(pwm0));
   uint64_t period_nsec, pulse_nsec;
   int ret;
   
   ret = pwm_capture_nsec(pwm_dev, 0,
                          PWM_CAPTURE_TYPE_BOTH | PWM_CAPTURE_MODE_SINGLE,
                          &period_nsec, &pulse_nsec, K_MSEC(1000));
   if (ret < 0) {
       printk("PWM capture failed: %d\n", ret);
       return ret;
   }
   
   printk("Period: %llu ns, Pulse: %llu ns\n", period_nsec, pulse_nsec);
   printk("Frequency: %llu Hz, Duty cycle: %llu%%\n",
          (uint64_t)NSEC_PER_SEC / period_nsec,
          (pulse_nsec * 100) / period_nsec);

Continuous Capture with Callback
=================================

For continuous capture, you can configure a callback function that will be invoked for each
captured PWM cycle:

.. code-block:: c
   :caption: Continuous PWM capture with callback

   void pwm_capture_callback(const struct device *dev, uint32_t channel,
                            uint32_t period_cycles, uint32_t pulse_cycles,
                            int status, void *user_data)
   {
       if (status < 0) {
           printk("Capture error: %d\n", status);
           return;
       }
       
       /* Convert cycles to time units if needed */
       printk("Captured: period=%u cycles, pulse=%u cycles\n",
              period_cycles, pulse_cycles);
   }

   int start_continuous_capture(void)
   {
       const struct device *pwm_dev = DEVICE_DT_GET(DT_NODELABEL(pwm0));
       int ret;
       
       /* Configure capture with callback */
       ret = pwm_configure_capture(pwm_dev, 0,
                                   PWM_CAPTURE_TYPE_BOTH | PWM_CAPTURE_MODE_CONTINUOUS,
                                   pwm_capture_callback, NULL);
       if (ret < 0) {
           return ret;
       }
       
       /* Enable capture */
       ret = pwm_enable_capture(pwm_dev, 0);
       if (ret < 0) {
           return ret;
       }
       
       return 0;
   }

Refer to :zephyr:code-sample:`capture` for a complete example of PWM capture functionality.

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
