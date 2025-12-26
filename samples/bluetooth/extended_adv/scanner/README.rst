.. zephyr:code-sample:: bluetooth_extended_advertising_scanner
   :name: Extended Advertising Scanner
   :relevant-api: bluetooth

   Scan for and connect to extended advertising devices.

Overview
********

This is the scanner part of the :zephyr:code-sample:`bluetooth_extended_advertising`
sample. This application acts as a Bluetooth LE scanner that scans for extended
advertisements and connects to the advertiser. After maintaining the connection,
it waits for the advertiser to disconnect, then cools down for 5 seconds before
restarting the scanning process.

See the main :zephyr:code-sample:`bluetooth_extended_advertising` sample documentation
for complete details on the extended advertising feature and the full connection/disconnection
cycle.

Requirements
************

* A board with Bluetooth Low Energy 5.0 support and extended advertising capability

Building and Running
********************

This application is part of a two-device sample. Build and flash this to one
device designated as the scanner, and build the advertiser application from
``samples/bluetooth/extended_adv/advertiser`` for the other device.

.. zephyr-app-commands::
   :zephyr-app: samples/bluetooth/extended_adv/scanner
   :board: nrf52840dk/nrf52840
   :goals: build flash
   :compact:

See :zephyr:code-sample:`bluetooth_extended_advertising` for expected output and more details.
