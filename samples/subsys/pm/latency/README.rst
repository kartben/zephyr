.. zephyr:code-sample:: pm_policy_latency
   :name: Power Management Latency Control
   :relevant-api: pm_policy_api

   Control power states using latency constraint APIs.

Overview
********

This sample demonstrates how to use the power management (PM) policy latency
APIs to control which power states the system can enter. By setting latency
constraints, applications and drivers can prevent the system from entering
power states that would exceed the specified wake-up latency requirements.

The sample demonstrates:

- Entering different power states without constraints (RUNTIME_IDLE, SUSPEND_TO_IDLE, STANDBY)
- Setting application-level latency constraints
- Device-level latency constraints
- Dynamic constraint updates
- Latency constraint subscription and notifications

Requirements
************

* A board with power management support
* Multiple power states defined with different latency characteristics

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/pm/latency
   :board: native_sim
   :goals: build run
   :compact:

Sample Output
=============

.. code-block:: console

   <inf> app: Sleeping for 1.1 seconds, we should enter RUNTIME_IDLE
   <inf> soc_pm: Entering RUNTIME_IDLE{0}
   <inf> app: Sleeping for 1.2 seconds, we should enter SUSPEND_TO_IDLE
   <inf> soc_pm: Entering SUSPEND_TO_IDLE{0}
   <inf> app: Sleeping for 1.3 seconds, we should enter STANDBY
   <inf> soc_pm: Entering STANDBY{0}
   <inf> app: Setting latency constraint: 30ms
   <inf> app: Sleeping for 1.1 seconds, we should enter RUNTIME_IDLE
   <inf> soc_pm: Entering RUNTIME_IDLE{0}
   <inf> app: Sleeping for 1.2 seconds, we should enter SUSPEND_TO_IDLE
   <inf> soc_pm: Entering SUSPEND_TO_IDLE{0}
   <inf> app: Sleeping for 1.3 seconds, we should enter SUSPEND_TO_IDLE
   <inf> soc_pm: Entering SUSPEND_TO_IDLE{0}
   ...
