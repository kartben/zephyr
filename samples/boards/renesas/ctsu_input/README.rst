.. zephyr:code-sample:: ctsu_input
   :name: Renesas CTSU Touch Input
   :relevant-api: input_interface

   Read touch buttons and sliders using the Renesas CTSU.

Overview
********

This sample demonstrates how to use the Renesas Capacitive Touch Sensing Unit
(CTSU) for touch input on Renesas RX-series boards. The CTSU enables capacitive
touch sensing for buttons and sliders without requiring external touch controller
chips.

The sample monitors touch events on buttons and a slider, and controls LEDs
in response to touch input. It demonstrates:

- Touch button detection
- Touch slider position tracking
- LED control based on touch events

Requirements
************

* A Renesas RX-series board with CTSU support (e.g., RSK-RX130)
* On-board touch buttons and/or touch slider
* LEDs for visual feedback

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/boards/renesas/ctsu_input
   :board: rsk_rx130_512kb
   :goals: build flash
   :compact:

Sample Output
=============

The sample responds to touch input by controlling LEDs. When you touch the
buttons or move your finger along the slider, the LEDs will change state
accordingly. Console output will show touch events as they occur.
