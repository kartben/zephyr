.. zephyr:code-sample:: fingerprint-wifi-direct
   :name: Fingerprint Wi-Fi Direct demo
   :relevant-api: biometrics_interface wifi_mgmt

   Authenticate an enrolled biometric user and immediately connect to a
   configured Wi-Fi Direct peer.

Overview
********

This sample combines the biometrics API with the Wi-Fi management API to build
an access-controlled Wi-Fi Direct flow:

1. Continuously scan for an enrolled biometric user.
2. Start Wi-Fi Direct peer discovery as soon as a valid user is identified.
3. Connect to the configured peer once it is discovered.

The sample uses Zephyr cleanup helpers to automatically tear down the temporary
network management callback and stop peer discovery on every exit path.

Requirements
************

* A supported biometric sensor exposed through the ``fingerprint`` devicetree alias
* A Wi-Fi device and driver stack with ``CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P`` support
* A peer MAC address configured through ``CONFIG_FINGERPRINT_WIFI_DIRECT_PEER_MAC``

On ``native_sim``, the sample uses the biometrics emulator so the authentication
path can run without manual interaction. ``native_sim`` does not provide a Wi-Fi
Direct interface, so the default configuration validates the biometric flow and
prints a reminder to rebuild with the Wi-Fi Direct profile on supported hardware.

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/biometrics/fingerprint_wifi_direct
   :board: <your_board>
   :goals: build flash
   :compact:

Before building for real hardware, set the peer MAC address to the desired
Wi-Fi Direct peer, for example with ``menuconfig`` or an extra configuration
fragment. Enable the Wi-Fi Direct profile by adding ``p2p.conf`` to the build:

.. code-block:: console

   west build -b <your_board> samples/drivers/biometrics/fingerprint_wifi_direct -- \
     -DCONF_FILE="prj.conf;p2p.conf"

Sample Output
*************

.. code-block:: console

   [00:00:00.000,000] <inf> main: Fingerprint Wi-Fi Direct demo
   [00:00:00.000,000] <inf> main: Waiting for an enrolled user...
   [00:00:00.050,000] <inf> main: User 1 authenticated (confidence 95, quality 90)
   [00:00:00.050,000] <inf> main: Searching for Wi-Fi Direct peer 02:00:00:00:00:01
   [00:00:02.100,000] <inf> main: Matched peer 02:00:00:00:00:01 (Lobby display)
   [00:00:02.300,000] <inf> main: Wi-Fi Direct connection established

Notes
*****

* The sample uses Wi-Fi Direct PBC provisioning.
* On real hardware, make sure the biometric sensor already has at least one
  enrolled user before running the demo.
* The default ``prj.conf`` keeps the sample runnable on ``native_sim``; add
  ``p2p.conf`` on boards that support Wi-Fi Direct.
