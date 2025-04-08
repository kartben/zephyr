.. _shield_mikroe_environment_click:

ENVIRONMENT Click Shield
========================

Overview
********

The ENVIRONMENT Click shield carries a ENVIRONMENT board from MikroElektronika.

.. figure:: images/environment-click.png
   :align: center
   :alt: ENVIRONMENT Click

   ENVIRONMENT Click

Requirements
************

This shield can only be used with a board which provides a configuration
for Arduino connectors and defines node aliases for Arduino's I2C and SPI.

Programming
**********

Set ``-DSHIELD=mikroe_environment_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/
   :board: nrf52840dk_nrf52840
   :shield: mikroe_environment_click
   :goals: build

References
**********

- `ENVIRONMENT Click webpage`_
- `ENVIRONMENT Click schematic`_

.. _ENVIRONMENT Click webpage: https://www.mikroe.com/environment-click
.. _ENVIRONMENT Click schematic: https://download.mikroe.com/documents/add-on-boards/click/environment-click/
