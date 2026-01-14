.. zephyr:code-sample:: spi_fujitsu_fram
   :name: Fujitsu SPI FRAM
   :relevant-api: spi_interface

   Access Fujitsu MB85RS64V FRAM over SPI.

Overview
********

This sample demonstrates how to access a Fujitsu MB85RS64V FRAM (Ferroelectric
Random Access Memory) module over SPI. FRAM is a non-volatile memory technology
that combines the benefits of RAM and ROM, offering fast write speeds and high
endurance.

The sample performs the following operations:

- Reads the manufacturer ID from the FRAM
- Writes test data to the FRAM
- Reads back the data and verifies it
- Demonstrates FRAM access patterns

Requirements
************

* A board with SPI support
* Fujitsu MB85RS64V FRAM module connected via SPI

Building and Running
********************

This sample can be built for any board with SPI support. Ensure the FRAM module
is properly connected to the SPI bus and the devicetree is configured correctly.

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/spi_fujitsu_fram
   :board: <your_board>
   :goals: build flash
   :compact:

Sample Output
=============

.. code-block:: console

   SPI FRAM Sample
   Testing Fujitsu MB85RS64V FRAM
   Write and read test passed
