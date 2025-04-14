.. _mikroe_surface_temp_click:

MikroElektronika SURFACE-TEMP Click
===================================

Overview
********

The SURFACE-TEMP Click shield carries a SURFACE-TEMP board from MikroElektronika.

.. figure:: images/mikroe_surface_temp_click.webp
   :align: center
   :alt: SURFACE-TEMP Click
   :height: 300px

   SURFACE-TEMP Click

Requirements
************

This shield can only be used with a board that provides a mikroBUS™ socket and defines a
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

- `SURFACE-TEMP Click webpage`_
- `SURFACE-TEMP Click schematic`_

.. _SURFACE-TEMP Click webpage: https://www.mikroe.com/surface-temp-click
.. _SURFACE-TEMP Click schematic: https://download.mikroe.com/documents/add-on-boards/click/surface-temp/surface-temp-click-schematic-v100.pdf
