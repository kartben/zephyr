.. _shield_mikroe_13dof_2_click:

13DOF-2 Click Shield
====================

Overview
********

The 13DOF-2 Click shield carries a 13DOF-2 board from MikroElektronika.

.. figure:: images/13dof-2-click.png
   :align: center
   :alt: 13DOF-2 Click

   13DOF-2 Click

Requirements
************

This shield can only be used with a board which provides a configuration
for Arduino connectors and defines node aliases for Arduino's I2C and SPI.

Programming
**********

Set ``-DSHIELD=mikroe_13dof_2_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/
   :board: nrf52840dk_nrf52840
   :shield: mikroe_13dof_2_click
   :goals: build

References
**********

- `13DOF-2 Click webpage`_
- `13DOF-2 Click schematic`_

.. _13DOF-2 Click webpage: https://www.mikroe.com/13dof-2-click
.. _13DOF-2 Click schematic: https://download.mikroe.com/documents/add-on-boards/click/13dof-2-click/
