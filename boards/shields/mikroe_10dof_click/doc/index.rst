.. _mikroe_10dof_click_shield:

MikroElektronika 10DOF Click
============================

Overview
********

`10DOF Click`_ is a mikroBUS add-on board for enhancing hardware prototypes with 10DOF functionality
(10 degrees of freedom). The click board carries two modules from Bosch: BNO055, a 9-axis absolute
orientation sensor and BMP180, a digital pressure sensor.

.. figure:: images/mikroe_10dof_click.webp
   :align: center
   :alt: 10DOF Click
   :height: 300px

   10DOF Click

Requirements
************


This shield can only be used with a board that provides a mikroBUS |trade| socket and defines a
``mikrobus_i2c`` node label for the mikroBUS™ I2C interface. See :ref:`shields` for more details.

Programming
**********

Set ``-DSHIELD=mikroe_10dof_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/sensor_shell
   :board: lpcxpresso55s16
   :shield: mikroe_10dof_click
   :goals: build

This will build the :zephyr:code-sample:`sensor_shell` sample which provides a quick way to verify
the shield is working correctly. After flashing, you can use the ``sensor`` command to list
available sensors and read their values.

References
**********

- `10DOF Click`_

.. _10DOF Click: https://www.mikroe.com/10dof-click
