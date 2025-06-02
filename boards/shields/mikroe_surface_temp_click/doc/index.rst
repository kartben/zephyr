.. _mikroe_surface_temp_click_shield:

MikroElektronika Surface Temp Click
===================================

Overview
********

`Surface Temp Click`_ is high accuracy digital temperature sensor Click board™, offering
breakthrough performance over a wide industrial range. It is equipped with the ADT7420 - an accurate
16-Bit Digital I2C temperature sensor from Analog Devices. It features high temperature accuracy,
ultralow temperature drift (0.0073°C), fast first temperature conversion on power-up, no temperature
calibration/correction required, and more. This makes the `Surface Temp Click`_ board™ a great
choice for RTD and thermistor replacement, medical equipment, food transportation and storage,
environmental monitoring, HVAC, and other applications.

.. figure:: images/mikroe_surface_temp_click.webp
   :align: center
   :alt: Surface Temp Click
   :height: 300px

   Surface Temp Click

Requirements
************


This shield can only be used with a board that provides a mikroBUS |trade| socket and defines a
``mikrobus_i2c`` node label for the mikroBUS™ I2C interface. See :ref:`shields` for more details.

Programming
**********

Set ``-DSHIELD=mikroe_surface_temp_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/sensor_shell
   :board: lpcxpresso55s16
   :shield: mikroe_surface_temp_click
   :goals: build

This will build the :zephyr:code-sample:`sensor_shell` sample which provides a quick way to verify
the shield is working correctly. After flashing, you can use the ``sensor`` command to list
available sensors and read their values.

References
**********

- `Surface Temp Click`_
- `Surface Temp Click schematic`_

.. _Surface Temp Click: https://www.mikroe.com/surface-temp-click
.. _Surface Temp Click schematic: https://download.mikroe.com/documents/add-on-boards/click/surface-temp/surface-temp-click-schematic-v100.pdf
