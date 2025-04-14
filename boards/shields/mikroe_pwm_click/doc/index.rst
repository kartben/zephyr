.. _mikroe_pwm_click:

MikroElektronika PWM Click
==========================

Overview
********

The PWM Click shield carries a PWM board from MikroElektronika.

.. figure:: images/mikroe_pwm_click.webp
   :align: center
   :alt: PWM Click
   :height: 300px

   PWM Click

Requirements
************

This shield can only be used with a board that provides a mikroBUS™ socket and defines a
``mikrobus_i2c`` node label for the mikroBUS™ I2C interface. See :ref:`shields` for more details.

Programming
**********

Set ``-DSHIELD=mikroe_pwm_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/sensor_shell
   :board: lpcxpresso55s16
   :shield: mikroe_pwm_click
   :goals: build

This will build the :zephyr:code-sample:`sensor_shell` sample which provides a quick way to verify
the shield is working correctly. After flashing, you can use the ``sensor`` command to list
available sensors and read their values.

References
**********

- `PWM Click webpage`_

.. _PWM Click webpage: https://www.mikroe.com/pwm-click
