.. zephyr:code-sample:: esp32_ethernet
   :name: ESP32 Ethernet
   :relevant-api: ethernet_interface

   Test Ethernet connectivity on ESP32-based boards.

Overview
********

This sample demonstrates Ethernet connectivity on ESP32 boards with Ethernet
support, such as the ESP32-Ethernet-Kit. The sample enables DHCP for automatic
IP address configuration and provides a network shell for testing connectivity.

The sample is minimal and relies on the network configuration subsystem to
automatically initialize the Ethernet interface and obtain an IP address via DHCP.

Requirements
************

* An ESP32-based board with Ethernet support (e.g., ESP32-Ethernet-Kit)
* Ethernet cable connected to a network with DHCP

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/boards/espressif/ethernet
   :board: esp32_ethernet_kit/esp32/procpu
   :goals: build flash
   :compact:

Sample Output
=============

After flashing and running the sample, you can use the network shell to test
connectivity:

.. code-block:: console

   uart:~$ net iface
   Interface 0x3ffb8e20 (Ethernet) [1]
   ===================================
   Link addr : 24:0A:C4:XX:XX:XX
   MTU       : 1500
   Flags     : AUTO_START,IPv4,ETHERNET
   Ethernet capabilities supported:
           10 Mbits
           100 Mbits
   IPv4 unicast addresses (max 1):
           192.168.1.100 autoconf preferred infinite
   IPv4 multicast addresses (max 2):
           224.0.0.1
   IPv4 gateway : 192.168.1.1
   IPv4 netmask : 255.255.255.0

Use shell commands like ``net ping <ip_address>`` to test connectivity.
