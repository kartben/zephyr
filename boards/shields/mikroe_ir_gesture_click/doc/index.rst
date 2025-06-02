.. _mikroe_ir_gesture_click_shield:

MikroElektronika IR Gesture Click
=================================

Overview
********

`IR Gesture Click`_ is a mikroBUS add-on board that enables contactless gesture recognition, along
with ambient light and proximity sensing capabilities with the APDS-9960 IC.

.. figure:: images/mikroe_ir_gesture_click.webp
   :align: center
   :alt: IR Gesture Click
   :height: 300px

   IR Gesture Click

Requirements
************


This shield can only be used with a board that provides a mikroBUS |trade| socket and defines a
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

- `IR Gesture Click`_

.. _IR Gesture Click: https://www.mikroe.com/ir-gesture-click
