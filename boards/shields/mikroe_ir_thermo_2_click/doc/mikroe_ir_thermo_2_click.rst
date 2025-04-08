.. _shield_mikroe_ir_thermo_2_click:

IR-THERMO-2 Click Shield
========================

Overview
********

The IR-THERMO-2 Click shield carries a IR-THERMO-2 board from MikroElektronika.

.. figure:: images/ir-thermo-2-click.png
   :align: center
   :alt: IR-THERMO-2 Click

   IR-THERMO-2 Click

Requirements
************

This shield can only be used with a board which provides a configuration
for Arduino connectors and defines node aliases for Arduino's I2C and SPI.

Programming
**********

Set ``-DSHIELD=mikroe_ir_thermo_2_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/
   :board: nrf52840dk_nrf52840
   :shield: mikroe_ir_thermo_2_click
   :goals: build

References
**********

- `IR-THERMO-2 Click webpage`_
- `IR-THERMO-2 Click schematic`_

.. _IR-THERMO-2 Click webpage: https://www.mikroe.com/ir-thermo-2-click
.. _IR-THERMO-2 Click schematic: https://download.mikroe.com/documents/add-on-boards/click/ir-thermo-2-click/
