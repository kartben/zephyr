.. _mikroe_altitude_2_click_shield:

MikroElektronika Altitude 2 Click
=================================

Overview
********

`Altitude 2 Click`_ is a high-resolution barometric pressure sensor add-on Click board™. It carries
the MS5607, a barometric pressure sensor IC with the stainless steel cap, produced by TE
Connectivity. This sensor provides very accurate measurements of temperature and atmospheric
pressure, which can be used to calculate the altitude with a very high resolution of 20cm per step.
Besides that, the Besides that, the device also includes features such as the ultra-low noise delta-
sigma 24bit ADC, low power consumption, fast conversion times, pre-programmed unique compensation
values, and more. Low count of external components requirement, along with the simple interface
which requires no extensive configuration programming, makes this sensor very attractive for
building altitude or air pressure measuring applications.

.. figure:: images/mikroe_altitude_2_click.webp
   :align: center
   :alt: Altitude 2 Click
   :height: 300px

   Altitude 2 Click

Requirements
************


This shield can only be used with a board that provides a mikroBUS |trade| socket and defines a
``mikrobus_i2c`` node label for the mikroBUS™ I2C interface. See :ref:`shields` for more details.

Programming
**********

Set ``-DSHIELD=mikroe_altitude_2_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/sensor_shell
   :board: lpcxpresso55s16
   :shield: mikroe_altitude_2_click
   :goals: build

This will build the :zephyr:code-sample:`sensor_shell` sample which provides a quick way to verify
the shield is working correctly. After flashing, you can use the ``sensor`` command to list
available sensors and read their values.

References
**********

- `Altitude 2 Click`_

.. _Altitude 2 Click: https://www.mikroe.com/altitude-2-click
