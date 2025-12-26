.. zephyr:code-sample:: nrf_sys_event
   :name: nRF System Events
   :relevant-api: nrf_sys_event

   Control constant latency mode using the nRF system event API.

Overview
********

This sample demonstrates how to use the nRF system event API to control
power management features on Nordic Semiconductor nRF devices. Specifically,
it shows how to request and release the global constant latency mode.

Constant latency mode is a power management feature that ensures predictable
and minimal latency at the cost of higher power consumption. This is useful
for applications that require deterministic timing.

The sample:

- Requests global constant latency mode
- Demonstrates reference counting by requesting it twice
- Releases the mode twice to show reference counting behavior
- Verifies that the mode is disabled when all requests are released

Requirements
************

* An nRF52, nRF53, or nRF54 series development kit

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/boards/nordic/nrf_sys_event
   :board: nrf52840dk/nrf52840
   :goals: build flash
   :compact:

Sample Output
=============

.. code-block:: console

   request global constant latency mode
   constant latency mode enabled
   request global constant latency mode again
   release global constant latency mode
   constant latency mode will remain enabled
   release global constant latency mode again
   constant latency mode will be disabled
   constant latency mode disabled
