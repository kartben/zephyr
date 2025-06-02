.. _mikroe_illuminance_click_shield:

MikroElektronika Illuminance Click
==================================

Overview
********

`Illuminance Click`_ carries a TSL2561 light-to-digital converter with a sensor that’s designed to
mimic the way humans perceive light.

.. figure:: images/mikroe_illuminance_click.webp
   :align: center
   :alt: Illuminance Click
   :height: 300px

   Illuminance Click

Requirements
************


This shield can only be used with a board that provides a mikroBUS |trade| socket and defines a
``mikrobus_i2c`` node label for the mikroBUS™ I2C interface. See :ref:`shields` for more details.

Programming
**********

Set ``-DSHIELD=mikroe_illuminance_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/sensor_shell
   :board: lpcxpresso55s16
   :shield: mikroe_illuminance_click
   :goals: build

This will build the :zephyr:code-sample:`sensor_shell` sample which provides a quick way to verify
the shield is working correctly. After flashing, you can use the ``sensor`` command to list
available sensors and read their values.

References
**********

- `Illuminance Click`_

.. _Illuminance Click: https://www.mikroe.com/illuminance-click
