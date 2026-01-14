.. zephyr:code-sample:: bluetooth_central_ead
   :name: Bluetooth Central Encrypted Advertising
   :relevant-api: bluetooth

   Scan for and decrypt encrypted advertising data.

Overview
********

This is the central part of the :zephyr:code-sample:`bluetooth_encrypted_advertising`
sample. This application acts as a Bluetooth LE central that scans for, connects to,
and pairs with the peripheral device. It receives encryption keys from the peripheral
and uses them to decrypt the encrypted advertising payloads.

See the main :zephyr:code-sample:`bluetooth_encrypted_advertising` sample documentation
for complete details on the encrypted advertising feature and hardware setup requirements.

Requirements
************

* A board with Bluetooth Low Energy support
* A board with a push button connected via a GPIO pin for pairing confirmation

Building and Running
********************

This application is part of a two-device sample. Build and flash this to one
device designated as the central, and build the peripheral application from
``samples/bluetooth/encrypted_advertising/peripheral`` for the other device.

.. zephyr-app-commands::
   :zephyr-app: samples/bluetooth/encrypted_advertising/central
   :board: nrf52840dk/nrf52840
   :goals: build flash
   :compact:

See :zephyr:code-sample:`bluetooth_encrypted_advertising` for expected output and more details.
