.. zephyr:code-sample:: wifi-p2p
   :name: Wi-Fi P2P (Wi-Fi Direct)
   :relevant-api: wifi_mgmt

   Discover nearby Wi-Fi Direct peers and establish a direct device-to-device connection.

Overview
********

This sample demonstrates :ref:`Wi-Fi P2P (Wi-Fi Direct) <wifi_mgmt_p2p>` support in Zephyr.
It registers event callbacks that log P2P lifecycle events (peer discovery,
GO negotiation, group formation and removal) and exposes the full ``wifi p2p``
shell command set for interactive exploration.

Wi-Fi Direct allows two devices to communicate directly without an access
point.  One device acts as the **Group Owner (GO)** (equivalent to a
software access point) and the other joins as a **client**.  The roles are
determined during a **GO Negotiation** phase that happens automatically
when ``wifi p2p connect`` is issued.

Requirements
************

A board that supports Wi-Fi with wpa_supplicant and has the
``CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P`` Kconfig option available (e.g.
``nrf7002dk/nrf5340/cpuapp`` or any board using the Nordic Wi-Fi driver).

Two boards running this sample are needed to perform a full P2P
connection test.  Alternatively, a Zephyr device can connect with any
commercial Wi-Fi Direct device (smartphone, laptop, printer, etc.).

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/net/wifi/p2p
   :board: nrf7002dk/nrf5340/cpuapp
   :goals: build flash
   :compact:

Sample Console Walkthrough
**************************

The sample uses the :ref:`Wi-Fi shell <wifi-shell>`.  Open a serial
terminal to the board and follow the steps below.

Step 1 — Start P2P discovery
=============================

.. code-block:: console

   uart:~$ wifi p2p find
   P2P find started

The device starts listening for P2P beacons/probe requests.
When a remote peer is found, the application logs a message:

.. code-block:: console

   [00:00:05.123,000] <inf> wifi_p2p: P2P peer found: MyPhone (f4:ce:36:01:00:38)

Step 2 — List discovered peers
================================

.. code-block:: console

   uart:~$ wifi p2p peer list
   Num | Name             | MAC               | Config Method
   ----+------------------+-------------------+--------------
   1   | MyPhone          | f4:ce:36:01:00:38 | PBC

Step 3 — Connect using Push Button Configuration (PBC)
=======================================================

Trigger PBC on the remote device (e.g., tap "Connect via Wi-Fi Direct" on
a smartphone), then from the Zephyr shell:

.. code-block:: console

   uart:~$ wifi p2p connect f4:ce:36:01:00:38 pbc
   P2P connection requested

The console will show GO negotiation progress:

.. code-block:: console

   [00:00:07.456,000] <inf> wifi_p2p: P2P GO negotiation request received
   [00:00:08.001,000] <inf> wifi_p2p: P2P group started

Step 3 (alternative) — Connect using PIN
==========================================

.. code-block:: console

   uart:~$ wifi p2p connect f4:ce:36:01:00:38 pin -g 0
   P2P PIN: 12345670

   uart:~$ wifi p2p connect f4:ce:36:01:00:38 pin 12345670
   P2P connection requested

Step 4 — Create an autonomous Group Owner
==========================================

To create a persistent group where this device always acts as GO:

.. code-block:: console

   uart:~$ wifi p2p group_add
   P2P group created on p2p-wlan0-0

Step 5 — Tear down the group
==============================

.. code-block:: console

   uart:~$ wifi p2p group_remove p2p-wlan0-0
   P2P group removed

   [00:00:15.789,000] <inf> wifi_p2p: P2P group removed

Configuration Options
*********************

:kconfig:option:`CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P`
   Enable Wi-Fi Direct support in the wpa_supplicant integration.

:kconfig:option:`CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P_LISTEN_CHANNEL`
   Social channel used for P2P listen state (1, 6, or 11).

:kconfig:option:`CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P_OPER_CHANNEL`
   Operating channel used when this device is the Group Owner.
