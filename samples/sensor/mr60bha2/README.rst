.. zephyr:code-sample:: mr60bha2
   :name: Seeed MR60BHA2 mmWave sensor
   :relevant-api: sensor_interface mr60bha2_interface

   Read presence, distance, breathing rate, heart rate, and target count from a
   Seeed MR60BHA2 mmWave radar sensor.

Overview
********

This sample fetches data from a :dtcompatible:`seeed,mr60bha2` sensor and prints:

* occupancy by reading the standard :c:enumerator:`SENSOR_CHAN_PROX` channel
* distance using the standard :c:enumerator:`SENSOR_CHAN_DISTANCE` channel
* breathing and heart rate using the
  :c:enumerator:`SENSOR_CHAN_MR60BHA2_BREATH_RATE` and
  :c:enumerator:`SENSOR_CHAN_MR60BHA2_HEART_RATE` custom channels
* tracked target count using the :c:enumerator:`SENSOR_CHAN_MR60BHA2_TARGET_COUNT`
  custom channel

Building and Running
********************

Build for the MR60BHA2 kit variant:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/mr60bha2
   :board: xiao_esp32c6/esp32c6/hpcore/mr60bha2
   :goals: build flash
