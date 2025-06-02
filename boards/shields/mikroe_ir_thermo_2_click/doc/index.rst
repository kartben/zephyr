.. _mikroe_ir_thermo_2_click_shield:

MikroElektronika IR Thermo 2 Click
==================================

Overview
********

The IR Thermo 2 Click shield carries a IR Thermo 2 board from MikroElektronika.

.. figure:: images/mikroe_ir_thermo_2_click.webp
   :align: center
   :alt: IR Thermo 2 Click
   :height: 300px

   IR Thermo 2 Click

Requirements
************


This shield can only be used with a board that provides a mikroBUS |trade| socket and defines a
``mikrobus_i2c`` node label for the mikroBUS™ I2C interface. See :ref:`shields` for more details.

Programming
**********

Set ``-DSHIELD=mikroe_ir_thermo_2_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/sensor_shell
   :board: lpcxpresso55s16
   :shield: mikroe_ir_thermo_2_click
   :goals: build

This will build the :zephyr:code-sample:`sensor_shell` sample which provides a quick way to verify
the shield is working correctly. After flashing, you can use the ``sensor`` command to list
available sensors and read their values.

References
**********

- `IR Thermo 2 Click`_

.. _IR Thermo 2 Click: https://www.mikroe.com/ir-thermo-2-click
