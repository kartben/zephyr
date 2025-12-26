.. zephyr:code-sample:: bluetooth_central_mtu_update
   :name: Bluetooth Central MTU Update
   :relevant-api: bt_gatt bluetooth

   Act as central in MTU exchange and receive large notifications.

Overview
********

This is the central part of the :zephyr:code-sample:`bluetooth_mtu_update` sample.
This application acts as a Bluetooth LE central that scans for and connects to
the peripheral device. Once connected, it initiates the MTU exchange, subscribes
to notifications, and receives large notifications from the peripheral to
demonstrate the updated MTU.

See the main :zephyr:code-sample:`bluetooth_mtu_update` sample documentation for
complete details on the MTU exchange process and hardware setup requirements.

Building and Running
********************

This application is part of a two-device sample. Build and flash this to one
device designated as the central, and build the peripheral application from
``samples/bluetooth/mtu_update/peripheral`` for the other device.

.. zephyr-app-commands::
   :zephyr-app: samples/bluetooth/mtu_update/central
   :board: qemu_cortex_m3
   :goals: build
   :compact:

See :zephyr:code-sample:`bluetooth_mtu_update` for expected output and more details.
