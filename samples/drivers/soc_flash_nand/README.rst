.. zephyr:code-sample:: soc_flash_nand
   :name: Cadence NAND Flash Driver
   :relevant-api: flash_interface

   Test the Cadence NAND flash driver functionality.

Overview
********

This sample demonstrates the usage of the Cadence NAND flash driver. It performs
basic flash operations including:

- Erasing flash blocks
- Writing data to flash pages
- Reading data from flash pages
- Verifying written data

The sample writes test data to multiple NAND flash pages, reads it back, and
verifies the data integrity. It also tests the erase functionality to ensure
proper operation.

Requirements
************

* A board with Cadence NAND flash controller support
* External NAND flash connected to the controller

Building and Running
********************

This sample is designed for boards with Cadence NAND flash controllers, such as
the Intel SoCFPGA Agilex 5 SoC Development Kit.

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/soc_flash_nand
   :board: intel_socfpga_agilex5_socdk
   :goals: build flash
   :compact:

Sample Output
=============

.. code-block:: console

   Nand flash driver test sample
   Nand flash driver block size 20000
   The Page size of 800
   Nand flash driver data erase successful....
   Nand flash driver write completed....
   Nand flash driver read completed....
   Nand flash driver read verified
   Nand flash driver data erase successful....
   Nand flash driver read verified after erase....
   Nand flash driver test sample completed....
