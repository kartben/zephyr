.. _shield_mikroe_accel_6_click:

ACCEL-6 Click Shield
====================

Overview
********

The ACCEL-6 Click shield carries a ACCEL-6 board from MikroElektronika.

.. figure:: images/accel-6-click.png
   :align: center
   :alt: ACCEL-6 Click

   ACCEL-6 Click

Requirements
************

This shield can only be used with a board which provides a configuration
for Arduino connectors and defines node aliases for Arduino's I2C and SPI.

Programming
**********

Set ``-DSHIELD=mikroe_accel_6_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/
   :board: nrf52840dk_nrf52840
   :shield: mikroe_accel_6_click
   :goals: build

References
**********

- `ACCEL-6 Click webpage`_
- `ACCEL-6 Click schematic`_

.. _ACCEL-6 Click webpage: https://www.mikroe.com/accel-6-click
.. _ACCEL-6 Click schematic: https://download.mikroe.com/documents/add-on-boards/click/accel-6-click/
