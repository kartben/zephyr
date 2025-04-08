.. _shield_mikroe_air_quality_3_click:

AIR-QUALITY-3 Click Shield
==========================

Overview
********

The AIR-QUALITY-3 Click shield carries a AIR-QUALITY-3 board from MikroElektronika.

.. figure:: images/air-quality-3-click.png
   :align: center
   :alt: AIR-QUALITY-3 Click

   AIR-QUALITY-3 Click

Requirements
************

This shield can only be used with a board which provides a configuration
for Arduino connectors and defines node aliases for Arduino's I2C and SPI.

Programming
**********

Set ``-DSHIELD=mikroe_air_quality_3_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/
   :board: nrf52840dk_nrf52840
   :shield: mikroe_air_quality_3_click
   :goals: build

References
**********

- `AIR-QUALITY-3 Click webpage`_
- `AIR-QUALITY-3 Click schematic`_

.. _AIR-QUALITY-3 Click webpage: https://www.mikroe.com/air-quality-3-click
.. _AIR-QUALITY-3 Click schematic: https://download.mikroe.com/documents/add-on-boards/click/air-quality-3-click/
