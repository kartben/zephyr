.. _mikroe_temp_hum_3_click_shield:

MikroElektronika TEMP HUM 3 CLICK Click
=======================================

Overview
********

Smart environmental temperature and humidity sensor

.. figure:: images/mikroe_temp_hum_3_click.webp
   :align: center
   :alt: TEMP HUM 3 CLICK Click
   :height: 300px

   TEMP HUM 3 CLICK Click

Requirements
************


This shield can only be used with a board that provides a mikroBUS |trade| socket and defines a
``mikrobus_i2c`` node label for the mikroBUS™ I2C interface. See :ref:`shields` for more details.

Programming
**********

Set ``-DSHIELD=mikroe_temp_hum_3_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/sensor_shell
   :board: lpcxpresso55s16
   :shield: mikroe_temp_hum_3_click
   :goals: build

This will build the :zephyr:code-sample:`sensor_shell` sample which provides a quick way to verify
the shield is working correctly. After flashing, you can use the ``sensor`` command to list
available sensors and read their values.

References
**********

- `TEMP HUM 3 CLICK Click`_
- `TEMP HUM 3 CLICK Click schematic`_

.. _TEMP HUM 3 CLICK Click: https://www.mikroe.com/temp-hum-3-click
.. _TEMP HUM 3 CLICK Click schematic: https://download.mikroe.com/documents/add-on-boards/click/temp-hum-3/temp-hum-3-click-schematic-v100.pdf
