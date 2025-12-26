.. zephyr:code-sample:: bluetooth_extended_advertising_advertiser
   :name: Extended Advertising Advertiser
   :relevant-api: bluetooth

   Advertise using Bluetooth LE extended advertising features.

Overview
********

This is the advertiser part of the :zephyr:code-sample:`bluetooth_extended_advertising`
sample. This application acts as a Bluetooth LE advertiser using the extended
advertising feature. It creates a connectable extended advertisement set and
waits for a scanner to connect. After a connection is established for 5 seconds,
it disconnects and immediately restarts the advertising process.

See the main :zephyr:code-sample:`bluetooth_extended_advertising` sample documentation
for complete details on the extended advertising feature and the full connection/disconnection
cycle.

Requirements
************

* A board with Bluetooth Low Energy 5.0 support and extended advertising capability

Building and Running
********************

This application is part of a two-device sample. Build and flash this to one
device designated as the advertiser, and build the scanner application from
``samples/bluetooth/extended_adv/scanner`` for the other device.

.. zephyr-app-commands::
   :zephyr-app: samples/bluetooth/extended_adv/advertiser
   :board: nrf52840dk/nrf52840
   :goals: build flash
   :compact:

See :zephyr:code-sample:`bluetooth_extended_advertising` for expected output and more details.
