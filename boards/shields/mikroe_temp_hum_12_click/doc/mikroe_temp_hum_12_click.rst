.. _shield_mikroe_temp_hum_12_click:

TEMP-HUM-12 Click Shield
========================

Overview
********

The TEMP-HUM-12 Click shield carries a TEMP-HUM-12 board from MikroElektronika.

.. figure:: images/temp-hum-12-click.png
   :align: center
   :alt: TEMP-HUM-12 Click

   TEMP-HUM-12 Click

Requirements
************

This shield can only be used with a board which provides a configuration
for Arduino connectors and defines node aliases for Arduino's I2C and SPI.

Programming
**********

Set ``-DSHIELD=mikroe_temp_hum_12_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/
   :board: nrf52840dk_nrf52840
   :shield: mikroe_temp_hum_12_click
   :goals: build

References
**********

- `TEMP-HUM-12 Click webpage`_
- `TEMP-HUM-12 Click schematic`_

.. _TEMP-HUM-12 Click webpage: https://www.mikroe.com/temp-hum-12-click
.. _TEMP-HUM-12 Click schematic: https://download.mikroe.com/documents/add-on-boards/click/temp-hum-12-click/
