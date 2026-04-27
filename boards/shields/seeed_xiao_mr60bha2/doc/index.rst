.. _seeed_xiao_mr60bha2:

Seeed Studio 60GHz mmWave Breathing and Heartbeat Sensor for XIAO
##################################################################

Overview
********

The `Seeed Studio MR60BHA2 mmWave Kit`_ pairs the MR60BHA2 60 GHz radar module with a Seeed XIAO
microcontroller board to detect human presence, breathing rate, and heartbeat rate.

.. figure:: img/seeed_xiao_mr60bha2.webp
   :align: center
   :alt: Seeed Studio MR60BHA2 mmWave Kit

   Seeed Studio MR60BHA2 mmWave Kit (Credit: Seeed Studio)

Requirements
************

This shield can be used with any board that exposes the XIAO connector labels and defines a
``xiao_serial`` node label. The MR60BHA2 module communicates over UART at 115200 baud using a
binary frame protocol.

The module connects to the standard D6/D7 UART pins of the XIAO connector, so no board-specific
pin-mux overrides are required.

Programming
***********

Set ``--shield seeed_xiao_mr60bha2`` when invoking ``west build``.

For example, using the :zephyr:code-sample:`mr60bha2` sample:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/mr60bha2
   :board: xiao_esp32c6/esp32c6/hpcore
   :shield: seeed_xiao_mr60bha2
   :goals: build

.. _Seeed Studio MR60BHA2 mmWave Kit:
   https://wiki.seeedstudio.com/getting_started_with_mr60bha2_mmwave_kit/
