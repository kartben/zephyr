.. zephyr:code-sample:: mr60bha2
   :name: Seeed Studio MR60BHA2
   :relevant-api: sensor_interface

   Read presence, breathing rate, and heartbeat rate from a Seeed Studio MR60BHA2 60 GHz mmWave
   sensor.

Overview
********

This sample reads and prints human presence status, breathing rate, and heartbeat rate from a
Seeed Studio MR60BHA2 60 GHz mmWave sensor connected via UART.

Requirements
************

This sample requires a board with the XIAO connector and the :ref:`seeed_xiao_mr60bha2` shield.

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/mr60bha2
   :board: xiao_esp32c6/esp32c6/hpcore
   :shield: seeed_xiao_mr60bha2
   :goals: build flash

Sample Output
*************

.. code-block:: console

   MR60BHA2 60 GHz mmWave Sensor
   ==============================

     Presence:         occupied
     Presence status:  Static target

     Breathing
     *********

     Phase:            Normal breathing
     Rate:             14.5 breaths/min

     Heartbeat
     *********

     Phase:            Normal heartbeat
     Rate:             72 bpm
