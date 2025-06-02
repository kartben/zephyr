.. _mikroe_13dof_2_click_shield:

MikroElektronika 13DOF 2 Click
==============================

Overview
********

`13DOF 2 Click`_ is an advanced 13-axis motion tracking Click board™, which utilizes two different
sensor ICs onboard: BME680, Volatile Organic Compounds (VOCs), humidity, pressure and temperature
sensor and BMX160, a 9-axis sensor consisting of a 3-axis, low-g accelerometer, a low power 3-axis
gyroscope and a 3-axis geomagnetic sensor. Both integrated sensor ICs are made by Bosch Sensortec,
featuring the state-of-the-art sensor technology processes, in order to fulfill the requirements for
immersive gaming and navigation applications, which require highly accurate sensor data fusion.
Besides that, `13DOF 2 Click`_ is also perfectly suited for use in many other applications such as
mobile phones, tablet PCs, GPS systems, Smart watches, Sport and fitness devices, and many more.

.. figure:: images/mikroe_13dof_2_click.webp
   :align: center
   :alt: 13DOF 2 Click
   :height: 300px

   13DOF 2 Click

Requirements
************


This shield can only be used with a board that provides a mikroBUS |trade| socket and defines a
``mikrobus_i2c`` node label for the mikroBUS™ I2C interface. See :ref:`shields` for more details.

Programming
**********

Set ``-DSHIELD=mikroe_13dof_2_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/sensor_shell
   :board: lpcxpresso55s16
   :shield: mikroe_13dof_2_click
   :goals: build

This will build the :zephyr:code-sample:`sensor_shell` sample which provides a quick way to verify
the shield is working correctly. After flashing, you can use the ``sensor`` command to list
available sensors and read their values.

References
**********

- `13DOF 2 Click`_
- `13DOF 2 Click schematic`_

.. _13DOF 2 Click: https://www.mikroe.com/13dof-2-click
.. _13DOF 2 Click schematic: https://download.mikroe.com/documents/add-on-boards/click/13dof_2_click/13DOF-2-click-schematic-v100.pdf
