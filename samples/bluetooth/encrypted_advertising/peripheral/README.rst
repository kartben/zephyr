.. zephyr:code-sample:: bluetooth_peripheral_ead
   :name: Bluetooth Peripheral Encrypted Advertising
   :relevant-api: bluetooth

   Advertise with encrypted data using the peripheral role.

Overview
********

This is the peripheral part of the :zephyr:code-sample:`bluetooth_encrypted_advertising`
sample. This application acts as a Bluetooth LE peripheral that advertises with
encrypted advertising data. It pairs with a central device, exchanges encryption
keys, and then sends encrypted advertising payloads.

See the main :zephyr:code-sample:`bluetooth_encrypted_advertising` sample documentation
for complete details on the encrypted advertising feature and hardware setup requirements.

Requirements
************

* A board with Bluetooth Low Energy support
* A board with a push button connected via a GPIO pin for pairing confirmation

Building and Running
********************

This application is part of a two-device sample. Build and flash this to one
device designated as the peripheral, and build the central application from
``samples/bluetooth/encrypted_advertising/central`` for the other device.

.. zephyr-app-commands::
   :zephyr-app: samples/bluetooth/encrypted_advertising/peripheral
   :board: nrf52840dk/nrf52840
   :goals: build flash
   :compact:

See :zephyr:code-sample:`bluetooth_encrypted_advertising` for expected output and more details.
