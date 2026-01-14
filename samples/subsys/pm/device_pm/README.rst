.. zephyr:code-sample:: device_pm
   :name: Device Power Management
   :relevant-api: device_pm_api

   Suspend and resume devices automatically to save power.

Overview
********

This sample demonstrates device runtime power management. It shows how devices
can be automatically suspended when idle and resumed when needed, helping to
reduce system power consumption.

The sample uses a dummy driver that implements the device PM interface. The
driver has parent-child relationships to demonstrate how power management
propagates through device hierarchies. When the application closes the device,
it is automatically suspended. When the device is accessed again, it is
automatically resumed.

The sample demonstrates:

- Automatic device suspension when idle
- Automatic device resumption when accessed
- Parent-child device PM relationships
- Coordinated suspend/resume of related devices

Requirements
************

* A board with power management support

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/pm/device_pm
   :board: qemu_x86
   :goals: build run
   :compact:

Sample Output
=============

.. code-block:: console

   parent suspending..
   child suspending..
   Device PM sample app start
   parent resuming..
   child resuming..
   Dummy device resumed
   child suspending..
   parent suspending..
   Device PM sample app complete
