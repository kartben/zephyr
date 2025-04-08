.. _shield_mikroe_lightranger_2_click:

LIGHTRANGER-2 Click Shield
==========================

Overview
********

The LIGHTRANGER-2 Click shield carries a LIGHTRANGER-2 board from MikroElektronika.

.. figure:: images/lightranger-2-click.png
   :align: center
   :alt: LIGHTRANGER-2 Click

   LIGHTRANGER-2 Click

Requirements
************

This shield can only be used with a board which provides a configuration
for Arduino connectors and defines node aliases for Arduino's I2C and SPI.

Programming
**********

Set ``-DSHIELD=mikroe_lightranger_2_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/
   :board: nrf52840dk_nrf52840
   :shield: mikroe_lightranger_2_click
   :goals: build

References
**********

- `LIGHTRANGER-2 Click webpage`_
- `LIGHTRANGER-2 Click schematic`_

.. _LIGHTRANGER-2 Click webpage: https://www.mikroe.com/lightranger-2-click
.. _LIGHTRANGER-2 Click schematic: https://download.mikroe.com/documents/add-on-boards/click/lightranger-2-click/
