.. _mikroe_ambient_2_click:

MikroElektronika AMBIENT-2 Click
================================

Overview
********

The AMBIENT-2 Click shield carries a AMBIENT-2 board from MikroElektronika.

.. figure:: images/mikroe_ambient_2_click.webp
   :align: center
   :alt: AMBIENT-2 Click
   :height: 300px

   AMBIENT-2 Click

Requirements
************

This shield can only be used with a board that provides a mikroBUS™ socket and defines a
``mikrobus_i2c`` node label for the mikroBUS™ I2C interface. See :ref:`shields` for more details.

Programming
**********

Set ``-DSHIELD=mikroe_ambient_2_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/sensor_shell
   :board: lpcxpresso55s16
   :shield: mikroe_ambient_2_click
   :goals: build

This will build the :zephyr:code-sample:`sensor_shell` sample which provides a quick way to verify
the shield is working correctly. After flashing, you can use the ``sensor`` command to list
available sensors and read their values.

References
**********

- `AMBIENT-2 Click webpage`_

.. _AMBIENT-2 Click webpage: https://www.mikroe.com/ambient-2-click
