.. zephyr:code-sample:: logging_multidomain
   :name: Multi-Domain Logging
   :relevant-api: logging_api ipc_service_api

   Demonstrate logging in multi-core/multi-domain systems.

Overview
********

This sample demonstrates logging in a multi-domain environment where multiple
cores or processing domains share a common logging infrastructure. It uses
the IPC (Inter-Processor Communication) service to transport log messages
between cores.

The sample consists of applications running on both the main core and a remote
core. Log messages from both cores are routed through a shared logging backend,
allowing unified logging output from the entire system.

This is useful for:

- Multi-core SoC debugging
- Centralized logging from heterogeneous processors
- System-wide log coordination

Requirements
************

* A multi-core board (e.g., nRF5340 DK)
* IPC support between cores

Building and Running
********************

This sample requires sysbuild to build applications for both cores.

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/logging/multidomain
   :board: nrf5340dk/nrf5340/cpuapp
   :goals: build flash
   :west-args: --sysbuild
   :compact:

Sample Output
=============

.. code-block:: console

   Hello World! nrf5340dk/nrf5340/cpuapp
   app: IPC-service HOST [INST 1] demo started
   app: loop: 0
   app: ipc open
   app: wait for bound
   app: bounded
   app: REMOTE [1]: 0
   app: HOST [1]: 1
   app: IPC-service HOST [INST 1] demo ended.
