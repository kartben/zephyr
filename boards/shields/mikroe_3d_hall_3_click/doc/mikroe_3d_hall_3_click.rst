.. _shield_mikroe_3d_hall_3_click:

3D-HALL-3 Click Shield
======================

Overview
********

The 3D-HALL-3 Click shield carries a 3D-HALL-3 board from MikroElektronika.

.. figure:: images/3d-hall-3-click.png
   :align: center
   :alt: 3D-HALL-3 Click

   3D-HALL-3 Click

Requirements
************

This shield can only be used with a board which provides a configuration
for Arduino connectors and defines node aliases for Arduino's I2C and SPI.

Programming
**********

Set ``-DSHIELD=mikroe_3d_hall_3_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/
   :board: nrf52840dk_nrf52840
   :shield: mikroe_3d_hall_3_click
   :goals: build

References
**********

- `3D-HALL-3 Click webpage`_
- `3D-HALL-3 Click schematic`_

.. _3D-HALL-3 Click webpage: https://www.mikroe.com/3d-hall-3-click
.. _3D-HALL-3 Click schematic: https://download.mikroe.com/documents/add-on-boards/click/3d-hall-3-click/
