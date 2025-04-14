.. _mikroe_ir_gesture_click:

MikroElektronika IR-GESTURE Click
=================================

Overview
********

The IR-GESTURE Click shield carries a IR-GESTURE board from MikroElektronika.

.. figure:: images/mikroe_ir_gesture_click.webp
   :align: center
   :alt: IR-GESTURE Click
   :height: 300px

   IR-GESTURE Click

Requirements
************

This shield can only be used with a board that provides a mikroBUS™ socket and defines a
``mikrobus_i2c`` node label for the mikroBUS™ I2C interface. See :ref:`shields` for more details.

Programming
**********

Set ``-DSHIELD=mikroe_ir_gesture_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/sensor_shell
   :board: lpcxpresso55s16
   :shield: mikroe_ir_gesture_click
   :goals: build

This will build the :zephyr:code-sample:`sensor_shell` sample which provides a quick way to verify
the shield is working correctly. After flashing, you can use the ``sensor`` command to list
available sensors and read their values.

References
**********

- `IR-GESTURE Click webpage`_

.. _IR-GESTURE Click webpage: https://www.mikroe.com/ir-gesture-click
