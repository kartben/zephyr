.. _shield_mikroe_pwm_click:

PWM Click Shield
================

Overview
********

The PWM Click shield carries a PWM board from MikroElektronika.

.. figure:: images/pwm-click.png
   :align: center
   :alt: PWM Click

   PWM Click

Requirements
************

This shield can only be used with a board which provides a configuration
for Arduino connectors and defines node aliases for Arduino's I2C and SPI.

Programming
**********

Set ``-DSHIELD=mikroe_pwm_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/
   :board: nrf52840dk_nrf52840
   :shield: mikroe_pwm_click
   :goals: build

References
**********

- `PWM Click webpage`_
- `PWM Click schematic`_

.. _PWM Click webpage: https://www.mikroe.com/pwm-click
.. _PWM Click schematic: https://download.mikroe.com/documents/add-on-boards/click/pwm-click/
