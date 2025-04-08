.. _shield_mikroe_6dof_imu_2_click:

6DOF-IMU-2 Click Shield
=======================

Overview
********

The 6DOF-IMU-2 Click shield carries a 6DOF-IMU-2 board from MikroElektronika.

.. figure:: images/6dof-imu-2-click.png
   :align: center
   :alt: 6DOF-IMU-2 Click

   6DOF-IMU-2 Click

Requirements
************

This shield can only be used with a board which provides a configuration
for Arduino connectors and defines node aliases for Arduino's I2C and SPI.

Programming
**********

Set ``-DSHIELD=mikroe_6dof_imu_2_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/
   :board: nrf52840dk_nrf52840
   :shield: mikroe_6dof_imu_2_click
   :goals: build

References
**********

- `6DOF-IMU-2 Click webpage`_
- `6DOF-IMU-2 Click schematic`_

.. _6DOF-IMU-2 Click webpage: https://www.mikroe.com/6dof-imu-2-click
.. _6DOF-IMU-2 Click schematic: https://download.mikroe.com/documents/add-on-boards/click/6dof-imu-2-click/
