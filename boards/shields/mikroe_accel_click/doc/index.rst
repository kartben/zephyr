.. _mikroe_accel_click_shield:

MikroElektronika Accel Click
============================

Overview
********

`Accel Click`_ is an accessory board in mikroBUS form factor. It features ADXL345 3-axis
accelerometer module with ultra-low power and high resolution (13-bit) measurement.

.. figure:: images/mikroe_accel_click.webp
   :align: center
   :alt: Accel Click
   :height: 300px

   Accel Click

Requirements
************


This shield can only be used with a board that provides a mikroBUS |trade| socket and defines a
``mikrobus_i2c`` node label for the mikroBUS™ I2C interface. See :ref:`shields` for more details.

Programming
**********

Set ``-DSHIELD=mikroe_accel_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/sensor_shell
   :board: lpcxpresso55s16
   :shield: mikroe_accel_click
   :goals: build

This will build the :zephyr:code-sample:`sensor_shell` sample which provides a quick way to verify
the shield is working correctly. After flashing, you can use the ``sensor`` command to list
available sensors and read their values.

References
**********

- `Accel Click`_

.. _Accel Click: https://www.mikroe.com/accel-click
