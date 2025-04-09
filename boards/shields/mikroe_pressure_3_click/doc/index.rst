.. _mikroe_pressure_3_click:

MikroElektronika PRESSURE-3 Click
=================================

Overview
********

The PRESSURE-3 Click shield carries a PRESSURE-3 board from MikroElektronika.

.. figure:: images/mikroe_pressure_3_click.webp
   :align: center
   :alt: PRESSURE-3 Click
   :height: 300px

   PRESSURE-3 Click

Requirements
************

This shield can only be used with a board that provides a mikroBUS™ socket and defines a
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

- `PRESSURE-3 Click webpage`_
- `PRESSURE-3 Click schematic`_

.. _PRESSURE-3 Click webpage: https://www.mikroe.com/pressure-3-click
.. _PRESSURE-3 Click schematic: https://download.mikroe.com/documents/add-on-boards/click/pressure-3/pressure-3-click-schematic-v100.pdf
