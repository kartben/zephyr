.. _mikroe_ambient_2_click_shield:

MikroElektronika Ambient 2 Click
================================

Overview
********

`Ambient 2 Click`_ carries TI’s OPT3001 Ambient Light sensor. It’s a small (2mm x 2mm) single-chip
lux meter that measures only the visible part of the light spectrum from any kind of source
(mimicking the way humans see light).

.. figure:: images/mikroe_ambient_2_click.webp
   :align: center
   :alt: Ambient 2 Click
   :height: 300px

   Ambient 2 Click

Requirements
************


This shield can only be used with a board that provides a mikroBUS |trade| socket and defines a
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

- `Ambient 2 Click`_

.. _Ambient 2 Click: https://www.mikroe.com/ambient-2-click
