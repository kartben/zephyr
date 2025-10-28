.. zephyr:code-sample:: ocpp
   :name: OCPP charge point
   :relevant-api: ocpp_api

   Implement an OCPP charge point that connects to a Central System server and
   simulates the meter readings.

Overview
********

Open Charge Point Protocol (OCPP) is an application protocol for communication
between Charge Points (Electric vehicle (EV) charging stations) and a central
management system, also known as a charging station network.

This ocpp sample application for Zephyr implements the OCPP library
and establishes a connection to an Central System server using the web socket

The source code for this sample application can be found at:
:zephyr_file:`samples/net/ocpp`.

USB High-Speed Data Logging Showcase
=====================================

This sample also demonstrates USB High-Speed (480 Mbps) capabilities by
implementing a USB CDC ACM interface that logs charging session data in
real-time. This showcases the high throughput possible with USB High-Speed,
which is particularly useful for:

- Real-time monitoring of charging sessions
- High-frequency meter reading data export
- Diagnostics and debugging of charging operations
- Fast data transfer for analysis tools

When the sample runs on a board with USB High-Speed support (like the
STM32F769I Discovery), you can connect to the USB CDC ACM port to see
detailed charging data being streamed at high speed, including:

- Charging session start/stop events
- Authorization status
- Real-time meter readings with timestamps
- Session statistics and throughput metrics

To view the USB data stream, connect to the USB CDC ACM device
(e.g., /dev/ttyACM0 on Linux) using a serial terminal:

.. code-block:: console

   minicom --device /dev/ttyACM0

Or using screen:

.. code-block:: console

   screen /dev/ttyACM0


Requirements
************

- Linux machine
- STM32 Discovery kit (32F769IDISCOVERY) or any network interface device
- SteVe Demo Server (<https://github.com/steve-community/steve/blob/master/README.md>)
- LAN for testing purposes (Ethernet)

Building and Running
********************

Build the ocpp sample application like this:

.. zephyr-app-commands::
   :zephyr-app: samples/net/ocpp
   :board: <board to use>
   :goals: build
   :compact:

The sample application is to built and tested on

.. code-block:: console

	west build -b stm32f769i_disco
	west flash

The output of sample is:

.. code-block:: console

	*** Booting Zephyr OS build v3.6.0-rc1-37-g8c035d8f24cf ***
	OCPP sample stm32f769i_disco
	[00:00:00.100,000] <inf> main: Initializing USB High-Speed CDC ACM logger...
	[00:00:00.150,000] <inf> main: USB High-Speed enabled - demonstrating fast data logging
	[00:00:02.642,000] <inf> net_dhcpv4: Received: 192.168.1.101
	[00:00:02.642,000] <inf> main: net mgr cb
	[00:00:02.642,000] <inf> main: Your address: 192.168.1.101
	[00:00:02.642,000] <inf> main: Lease time: 86400 seconds
	[00:00:02.642,000] <inf> main: Subnet: 255.255.255.0
	[00:00:02.643,000] <inf> main: Router: 192.168.1.1
	[00:00:07.011,000] <inf> main: cs server 122.165.245.213 8180
	[00:00:07.011,000] <inf> main: IPv4 Address 122.165.245.213
	[00:00:07.024,000] <inf> main: sntp succ since Epoch: 1707890823
	[00:00:07.024,000] <inf> ocpp: upstream init
	[00:00:07.025,000] <inf> ocpp: ocpp init success
	[00:00:17.066,000] <inf> main: ocpp auth 0> idcon 1 status 1
	[00:00:17.101,000] <inf> main: ocpp auth 0> idcon 2 status 1
	[00:00:17.197,000] <inf> main: ocpp start charging connector id 1
	[00:00:17.255,000] <inf> main: ocpp start charging connector id 2
	[00:01:07.064,000] <inf> main: ocpp stop charging connector id 1
	[00:01:08.063,000] <inf> main: ocpp stop charging connector id 2

USB CDC ACM Output
==================

When connected to the USB CDC ACM port, you will see real-time charging data:

.. code-block:: console

	=== OCPP USB High-Speed Logger ===
	This showcases USB High-Speed (480 Mbps) data transfer
	Logging charging session data in real-time

	[START] Connector 1: ID=ZepId00
	[AUTH] Connector 1: Authorized (status=1)
	[METER] Conn1: 7 Wh (reading #1)
	[METER] Conn1: 8 Wh (reading #2)
	[START] Connector 2: ID=ZepId01
	[AUTH] Connector 2: Authorized (status=1)
	[METER] Conn2: 9 Wh (reading #3)
	[METER] Conn1: 10 Wh (reading #4)
	...
	[STOP] Connector 1: Session ended
	[STOP] Connector 2: Session ended

	=== Session Summary ===
	Total meter readings: 120
	USB data transferred: 8450 bytes
	Average throughput: ~264 bytes/sec
	USB High-Speed (480 Mbps) enables efficient data logging

The high-speed USB interface demonstrates real-time data streaming capabilities,
which is valuable for monitoring and diagnostics in production charging stations.

