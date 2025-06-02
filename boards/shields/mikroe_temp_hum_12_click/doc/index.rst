.. _mikroe_temp_hum_12_click_shield:

MikroElektronika TEMP HUM 12 CLICK Click
========================================

Overview
********

The TEMP HUM 12 CLICK Click shield carries a TEMP HUM 12 CLICK board from MikroElektronika.

.. figure:: images/mikroe_temp_hum_12_click.webp
   :align: center
   :alt: TEMP HUM 12 CLICK Click
   :height: 300px

   TEMP HUM 12 CLICK Click

Requirements
************


This shield can only be used with a board that provides a mikroBUS |trade| socket and defines a
``mikrobus_i2c`` node label for the mikroBUS™ I2C interface. See :ref:`shields` for more details.

Programming
**********

Set ``-DSHIELD=mikroe_temp_hum_12_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/sensor_shell
   :board: lpcxpresso55s16
   :shield: mikroe_temp_hum_12_click
   :goals: build

This will build the :zephyr:code-sample:`sensor_shell` sample which provides a quick way to verify
the shield is working correctly. After flashing, you can use the ``sensor`` command to list
available sensors and read their values.

References
**********

- `TEMP HUM 12 CLICK Click`_

.. _TEMP HUM 12 CLICK Click: https://www.mikroe.com/temp-hum-12-click
