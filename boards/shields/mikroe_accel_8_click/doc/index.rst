.. _mikroe_accel_8_click_shield:

MikroElektronika Accel 8 Click
==============================

Overview
********

`Accel 8 Click`_ is an advanced 6-axis motion tracking Click board™, which utilizes the MPU6050, a
very popular motion sensor IC, equipped with a 3-axis gyroscope and 3-axis accelerometer. It also
features Digital Motion Processor™, a very powerful processing engine, which reduces the firmware
complexity and the processing load of the host MCU. The output of each axis is digitized by a
separate 16-bit ADC and processed by the DMP engine, offering motion data over the I2C Interface.
Packed with a set of very powerful options, this Click board™ represents an ideal solution for the
development of MotionInterface™ based applications.

.. figure:: images/mikroe_accel_8_click.webp
   :align: center
   :alt: Accel 8 Click
   :height: 300px

   Accel 8 Click

Requirements
************


This shield can only be used with a board that provides a mikroBUS |trade| socket and defines a
``mikrobus_i2c`` node label for the mikroBUS™ I2C interface. See :ref:`shields` for more details.

Programming
**********

Set ``-DSHIELD=mikroe_accel_8_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/sensor_shell
   :board: lpcxpresso55s16
   :shield: mikroe_accel_8_click
   :goals: build

This will build the :zephyr:code-sample:`sensor_shell` sample which provides a quick way to verify
the shield is working correctly. After flashing, you can use the ``sensor`` command to list
available sensors and read their values.

References
**********

- `Accel 8 Click`_
- `Accel 8 Click schematic`_

.. _Accel 8 Click: https://www.mikroe.com/accel-8-click
.. _Accel 8 Click schematic: https://download.mikroe.com/documents/add-on-boards/click/accel-8/accel-8-schematic-v100.pdf
