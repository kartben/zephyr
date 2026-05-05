.. _pwm_api:

Pulse Width Modulation (PWM)
############################

Overview
********

The PWM (Pulse Width Modulation) driver API provides control over PWM output
signals. Applications can set the period and pulse width (duty cycle) of each
PWM channel in nanoseconds. The API also supports inverting signal polarity,
capturing input PWM signals to measure their period and pulse width, and
configuring multi-channel PWM devices.

Common use cases include motor control, LED dimming, servo positioning, and
generating audio tones.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_PWM`

API Reference
*************

.. doxygengroup:: pwm_interface
