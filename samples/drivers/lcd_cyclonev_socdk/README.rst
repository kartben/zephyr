.. zephyr:code-sample:: lcd_cyclonev_socdk
   :name: Cyclone V SoC LCD Display
   :relevant-api: i2c_interface

   Display text on the LCD of the Cyclone V SoC Development Kit.

Overview
********

This sample demonstrates how to use the LCD display on the Cyclone V SoC FPGA
Development Kit. The LCD is controlled via I2C and the sample shows how to send
commands and display text on the screen.

The sample displays "Hello World!" on the LCD screen using the I2C interface
to communicate with the LCD controller.

Requirements
************

* Cyclone V SoC Development Kit with LCD display
* The LCD must be connected to the I2C bus

Building and Running
********************

This sample is specifically designed for the Cyclone V SoC Development Kit.

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/lcd_cyclonev_socdk
   :board: cyclonev_socdk
   :goals: build flash
   :compact:

Sample Output
=============

The sample displays "Hello World!" on the LCD screen and also prints to console:

.. code-block:: console

   Hello World! cyclonev_socdk
