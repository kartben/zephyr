.. zephyr:code-sample:: wifi-direct-hotspot-biometrics
   :name: Wi-Fi Direct hotspot with biometrics
   :relevant-api: wifi_mgmt biometrics_interface gpio_interface

   Expose a Wi-Fi Direct hotspot and gate peer access behind biometrics authentication.

Overview
********

This sample starts an autonomous Wi-Fi Direct group-owner hotspot and waits for peers to
join. When a peer connects, the sample blinks ``led0`` while it waits for
``biometric_match()`` to approve the connection. If biometric verification fails, the sample
requests that the peer be disconnected.

The default configuration uses the biometrics emulator and seeds a demo template at boot so
the authentication flow can be exercised without dedicated sensor hardware.

On ``native_sim`` there is no Wi-Fi interface, so the sample falls back to a simulated peer
attempt. That allows the LED and biometrics flow to be exercised locally while the same
application logic still uses the real Wi-Fi Direct path on supported boards.

Requirements
************

* A board with Wi-Fi Direct / P2P support for real hotspot operation
* A board-provided ``led0`` alias

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/net/wifi/direct_hotspot_biometrics
   :board: esp32_devkitc/esp32/procpu
   :goals: build flash
   :compact:

For local host validation of the biometrics flow:

.. zephyr-app-commands::
   :zephyr-app: samples/net/wifi/direct_hotspot_biometrics
   :board: native_sim
   :goals: build run
   :compact:

Sample Output
*************

.. code-block:: console

   [00:00:00.000,000] <inf> wifi_direct_biometrics: Seeded demo biometric template 1
   [00:00:00.000,000] <wrn> wifi_direct_biometrics: No Wi-Fi interface found, simulating a peer attempt
   [00:00:01.000,000] <inf> wifi_direct_biometrics: Peer 02:00:00:00:00:01 requested access
   [00:00:01.000,000] <inf> wifi_direct_biometrics: Blinking led0 while waiting for biometric approval
   [00:00:01.001,000] <inf> wifi_direct_biometrics: Peer 02:00:00:00:00:01 authenticated (confidence 90, quality 95)
