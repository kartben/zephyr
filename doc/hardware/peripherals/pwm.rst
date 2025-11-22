.. _pwm_api:

Pulse Width Modulation (PWM)
############################

Overview
********

Pulse Width Modulation (PWM) is a technique for generating analog-like output using digital signals.
PWM signals rapidly switch between high and low states, with the duty cycle (percentage of time in
the high state) determining the effective output level. PWM is commonly used for LED dimming,
motor speed control, and generating analog signals.

The PWM API provides a generic interface for PWM controllers. Key features include:

**Signal Generation**
  Configure PWM period (frequency) and pulse width (duty cycle).
  Generate precise timing signals for various applications.

**Multiple Channels**
  Control multiple independent PWM channels from a single controller.
  Each channel can have different period and duty cycle settings.

**Capture Mode**
  Capture PWM signals from external sources (hardware-dependent).
  Measure period and pulse width of incoming signals.

**Devicetree Integration**
  PWM outputs are defined in the Devicetree using the :c:struct:`pwm_dt_spec` structure.
  Hardware-agnostic pin configuration and channel selection.

Devicetree Configuration
************************

PWM controllers are defined in the Devicetree as nodes with a ``pwm-controller`` property.
The ``#pwm-cells`` property typically specifies that 2 or 3 cells are used: channel number,
period, and optional flags.

Example of a PWM controller definition:

.. code-block:: devicetree

   pwm0: pwm@40014000 {
       compatible = "st,stm32-pwm";
       reg = <0x40014000 0x400>;
       #pwm-cells = <3>;
       status = "okay";
   };

Example of defining a PWM LED:

.. code-block:: devicetree

   pwm_leds {
       compatible = "pwm-leds";
       pwm_led0: pwm_led_0 {
           pwms = <&pwm0 1 PWM_MSEC(20) PWM_POLARITY_NORMAL>;
           label = "PWM LED0";
       };
   };

   aliases {
       pwm-led0 = &pwm_led0;
   };

Basic Operation
***************

PWM operations are usually performed using a :c:struct:`pwm_dt_spec` structure, which
contains the PWM device, channel, period, and flags from the Devicetree.

.. code-block:: c
   :caption: Configure and use a PWM output

   #include <zephyr/drivers/pwm.h>

   #define PWM_LED0_NODE DT_ALIAS(pwm_led0)

   static const struct pwm_dt_spec pwm_led = PWM_DT_SPEC_GET(PWM_LED0_NODE);

   void setup_pwm(void)
   {
       uint32_t period_usec = 20000U; /* 20ms period (50 Hz) */
       uint32_t pulse_usec;
       int ret;

       if (!pwm_is_ready_dt(&pwm_led)) {
           printk("PWM device not ready\n");
           return;
       }

       /* Set PWM to 50% duty cycle */
       pulse_usec = period_usec / 2U;
       ret = pwm_set_dt(&pwm_led, PWM_USEC(period_usec), PWM_USEC(pulse_usec));
       if (ret < 0) {
           printk("Failed to set PWM: %d\n", ret);
           return;
       }

       /* Gradually increase brightness (duty cycle) */
       for (pulse_usec = 0; pulse_usec <= period_usec; pulse_usec += 100) {
           pwm_set_dt(&pwm_led, PWM_USEC(period_usec), PWM_USEC(pulse_usec));
           k_sleep(K_MSEC(10));
       }
   }

Refer to :zephyr:code-sample:`pwm-blinky` for a complete example of using the PWM API.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_PWM`
* :kconfig:option:`CONFIG_PWM_SHELL`
* :kconfig:option:`CONFIG_PWM_CAPTURE`

API Reference
*************

.. doxygengroup:: pwm_interface
