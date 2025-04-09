.. _mikroe_lightranger_2_click:

MikroElektronika LIGHTRANGER-2 Click
====================================

Overview
********

The LIGHTRANGER-2 Click shield carries a LIGHTRANGER-2 board from MikroElektronika.

.. figure:: images/mikroe_lightranger_2_click.webp
   :align: center
   :alt: LIGHTRANGER-2 Click
   :height: 300px

   LIGHTRANGER-2 Click

Requirements
************

This shield can only be used with a board that provides a mikroBUS™ socket and defines a
``mikrobus_i2c`` node label for the mikroBUS™ I2C interface. See :ref:`shields` for more details.

Programming
**********

Set ``-DSHIELD=mikroe_lightranger_2_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/sensor_shell
   :board: lpcxpresso55s16
   :shield: mikroe_lightranger_2_click
   :goals: build

This will build the :zephyr:code-sample:`sensor_shell` sample which provides a quick way to verify
the shield is working correctly. After flashing, you can use the ``sensor`` command to list
available sensors and read their values.

References
**********

- `LIGHTRANGER-2 Click webpage`_
- `LIGHTRANGER-2 Click schematic`_

.. _LIGHTRANGER-2 Click webpage: https://www.mikroe.com/lightranger-2-click
.. _LIGHTRANGER-2 Click schematic: https://download.mikroe.com/documents/add-on-boards/click/lightranger-2/light-ranger-2-click-schematic-v100.pdf
