.. zephyr:code-sample:: sx9500
   :name: SX9500 Proximity Sensor
   :relevant-api: sensor_interface

   Read proximity data from an SX9500 SAR sensor.

Overview
********

This sample demonstrates how to use the SX9500 SAR (Specific Absorption Rate)
proximity sensor driver. The sample periodically reads proximity data from the
sensor and displays it on the console.

The sample supports both polling and trigger modes, depending on the
configuration. In trigger mode, the application waits for near/far events
from the sensor, while in polling mode, it actively queries the sensor
at regular intervals.

Requirements
************

This sample requires an SX9500 proximity sensor connected via I2C. The sensor
must be properly defined in the board's devicetree.

Building and Running
********************

This sample can be built for boards that have an SX9500 sensor connected.
An example board overlay is provided for the FRDM-K64F board.

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/sx9500
   :board: frdm_k64f
   :goals: build flash
   :compact:

Sample Output
=============

.. code-block:: console

   device is 0x20003a48, name is SX9500
   prox is 0
   prox is 1
   prox is 1
   prox is 0

   <repeats endlessly>
