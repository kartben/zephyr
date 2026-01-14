.. zephyr:code-sample:: bluetooth_peripheral_mtu_update
   :name: Bluetooth Peripheral MTU Update
   :relevant-api: bt_gatt bluetooth

   Act as peripheral in MTU exchange and send large notifications.

Overview
********

This is the peripheral part of the :zephyr:code-sample:`bluetooth_mtu_update` sample.
This application acts as a Bluetooth LE peripheral that advertises and waits for
a central device to connect. Once connected, it exchanges MTU values with the
central and sends a large notification to demonstrate the updated MTU.

See the main :zephyr:code-sample:`bluetooth_mtu_update` sample documentation for
complete details on the MTU exchange process and hardware setup requirements.

Building and Running
********************

This application is part of a two-device sample. Build and flash this to one
device designated as the peripheral, and build the central application from
``samples/bluetooth/mtu_update/central`` for the other device.

.. zephyr-app-commands::
   :zephyr-app: samples/bluetooth/mtu_update/peripheral
   :board: qemu_cortex_m3
   :goals: build
   :compact:

See :zephyr:code-sample:`bluetooth_mtu_update` for expected output and more details.
