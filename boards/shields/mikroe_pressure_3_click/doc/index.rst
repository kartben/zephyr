.. _mikroe_pressure_3_click_shield:

MikroElektronika Pressure 3 Click
=================================

Overview
********

`Pressure 3 Click`_ is a mikroBUS add-on board that carries an Infineon DPS310 digital barometric
pressure sensor. It has an operating range from 300 to 1200 hPa with a relative accuracy of 0.06 hPa
and absolute accuracy of 1 hPa.

.. figure:: images/mikroe_pressure_3_click.webp
   :align: center
   :alt: Pressure 3 Click
   :height: 300px

   Pressure 3 Click

Requirements
************


This shield can only be used with a board that provides a mikroBUS |trade| socket and defines a
``mikrobus_i2c`` node label for the mikroBUS™ I2C interface. See :ref:`shields` for more details.

Programming
**********

Set ``-DSHIELD=mikroe_pressure_3_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/sensor_shell
   :board: lpcxpresso55s16
   :shield: mikroe_pressure_3_click
   :goals: build

This will build the :zephyr:code-sample:`sensor_shell` sample which provides a quick way to verify
the shield is working correctly. After flashing, you can use the ``sensor`` command to list
available sensors and read their values.

References
**********

- `Pressure 3 Click`_
- `Pressure 3 Click schematic`_

.. _Pressure 3 Click: https://www.mikroe.com/pressure-3-click
.. _Pressure 3 Click schematic: https://download.mikroe.com/documents/add-on-boards/click/pressure-3/pressure-3-click-schematic-v100.pdf
