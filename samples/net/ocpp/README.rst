.. zephyr:code-sample:: ocpp
   :name: OCPP charge point
   :relevant-api: ocpp_api

   Implement an OCPP charge point that connects to a Central System server and
   simulates charging transactions with meter readings.

Overview
********

Open Charge Point Protocol (OCPP) is an application protocol for communication
between Charge Points (Electric vehicle (EV) charging stations) and a central
management system, also known as a charging station network. OCPP is a
`standard <https://openchargealliance.org/protocols/open-charge-point-protocol/>`_
defined by The Open Charge Alliance.

This OCPP sample application for Zephyr implements an OCPP 1.6 Charge Point
client with the core profile. It establishes a WebSocket connection to a
Central System server and demonstrates:

* Charge Point registration and boot notification
* Authorization of charging sessions using ID tags
* Starting and stopping charging transactions
* Periodic meter value sampling and reporting
* Handling remote start/stop commands from the Central System
* Multi-connector support (2 connectors in the sample)

The sample simulates two charging connectors that can independently start and
stop charging sessions. It uses ZBUS for inter-thread communication and
demonstrates the full lifecycle of OCPP charging transactions.

The source code for this sample application can be found at:
:zephyr_file:`samples/net/ocpp`.

Requirements
************

- Linux machine for running the Central System server
- Network-capable Zephyr board (e.g., STM32 Discovery kit 32F769IDISCOVERY) or QEMU
- SteVe OCPP Central System server (https://github.com/steve-community/steve)
- SNTP server (for time synchronization)
- Ethernet or Wi-Fi network connection

Setting up the Central System Server
*************************************

The OCPP Charge Point requires a Central System (CS) server to connect to.
For development and testing, you can use the open-source SteVe server.

Installing and Running SteVe
=============================

1. Clone the SteVe repository:

   .. code-block:: console

      $ git clone https://github.com/steve-community/steve.git
      $ cd steve

2. Build and run SteVe using Maven:

   .. code-block:: console

      $ mvn package
      $ java -jar target/steve.jar

   By default, SteVe will run on port 8080 (web interface) and 8180 (WebSocket).

3. Access the SteVe web interface at http://localhost:8080/steve

4. Configure a Charge Point in SteVe:

   * Navigate to "Data Management" → "Charge Points" → "Add"
   * Enter Charge Point ID: ``zephyr``
   * This ID must match the endpoint in the sample configuration

Configuration
*************

Before building the sample, you need to configure the server addresses in
:file:`prj.conf`:

.. code-block:: cfg

   CONFIG_NET_SAMPLE_SNTP_SERVER="pool.ntp.org"
   CONFIG_NET_SAMPLE_OCPP_SERVER="192.168.1.100"
   CONFIG_NET_SAMPLE_OCPP_PORT=8180

Replace ``192.168.1.100`` with the IP address of your Central System server.

Kconfig Options
===============

The sample uses several important configuration options:

Network Configuration
---------------------

* :kconfig:option:`CONFIG_NETWORKING`: Enable networking support
* :kconfig:option:`CONFIG_NET_IPV4`: Enable IPv4 networking
* :kconfig:option:`CONFIG_NET_TCP`: Enable TCP protocol support
* :kconfig:option:`CONFIG_NET_DHCPV4`: Enable DHCPv4 client for automatic IP configuration
* :kconfig:option:`CONFIG_NET_SOCKETS`: Enable BSD sockets API

Protocol Support
----------------

* :kconfig:option:`CONFIG_HTTP_CLIENT`: Enable HTTP client (required for WebSocket)
* :kconfig:option:`CONFIG_WEBSOCKET_CLIENT`: Enable WebSocket client support
* :kconfig:option:`CONFIG_OCPP`: Enable OCPP library support
* :kconfig:option:`CONFIG_SNTP`: Enable SNTP client for time synchronization
* :kconfig:option:`CONFIG_JSON_LIBRARY`: Enable JSON library (required by OCPP)

OCPP-Specific Options
---------------------

These options can be configured in the OCPP subsystem:

* :kconfig:option:`CONFIG_OCPP_INT_THREAD_STACKSIZE`: OCPP internal thread stack size (default: 4096)
* :kconfig:option:`CONFIG_OCPP_WSREADER_THREAD_STACKSIZE`: WebSocket reader thread stack size (default: 4096)
* :kconfig:option:`CONFIG_OCPP_RECV_BUFFER_SIZE`: WebSocket receive buffer size (default: 2048)
* :kconfig:option:`CONFIG_OCPP_INTERNAL_MSGQ_CNT`: Internal message queue count (default: 10)
* :kconfig:option:`CONFIG_OCPP_MSG_JSON`: Use JSON format for PDU messages (enabled by default)

Optional OCPP Profiles
----------------------

The OCPP library supports optional profiles that can be enabled:

* :kconfig:option:`CONFIG_OCPP_PROFILE_SMART_CHARGE`: Enable Smart Charging profile
* :kconfig:option:`CONFIG_OCPP_PROFILE_REMOTE_TRIG`: Enable Remote Trigger profile
* :kconfig:option:`CONFIG_OCPP_PROFILE_RESERVATION`: Enable Reservation profile
* :kconfig:option:`CONFIG_OCPP_PROFILE_LOCAL_AUTH_LIST`: Enable Local Authorization List profile
* :kconfig:option:`CONFIG_OCPP_PROFILE_FIRMWARE_MGNT`: Enable Firmware Management profile

Resource Requirements
---------------------

* :kconfig:option:`CONFIG_MAIN_STACK_SIZE`: Main thread stack size (4096 bytes)
* :kconfig:option:`CONFIG_HEAP_MEM_POOL_SIZE`: Heap memory pool size (15000 bytes)
* :kconfig:option:`CONFIG_NET_TX_STACK_SIZE`: Network TX stack size (2048 bytes)
* :kconfig:option:`CONFIG_NET_RX_STACK_SIZE`: Network RX stack size (2048 bytes)

Building and Running
********************

Build the OCPP sample application:

.. zephyr-app-commands::
   :zephyr-app: samples/net/ocpp
   :board: <board to use>
   :goals: build
   :compact:

For example, to build for the STM32F769I Discovery board:

.. code-block:: console

   west build -b stm32f769i_disco samples/net/ocpp
   west flash

Application Behavior
********************

The sample application demonstrates the following OCPP workflow:

1. **Network Initialization**

   * Waits for network interface to be ready
   * Obtains IP address via DHCP
   * Synchronizes time using SNTP

2. **OCPP Initialization**

   * Resolves Central System server hostname to IP address
   * Initializes OCPP library with Charge Point information
   * Establishes WebSocket connection to Central System
   * Sends BootNotification message

3. **Charging Session Lifecycle**

   For each of the two connectors, the sample:

   * Opens an OCPP session (required for each physical connector)
   * Sends Authorize request with ID tag (e.g., "ZepId00", "ZepId01")
   * On successful authorization, starts a charging transaction
   * Periodically reports meter values (energy consumption) to Central System
   * After 30 seconds, stops the charging transaction
   * Closes the OCPP session

4. **Remote Control Support**

   The application can also respond to remote commands from the Central System:

   * Remote Start Transaction: Initiates charging on a specific connector
   * Remote Stop Transaction: Stops ongoing charging session
   * Unlock Connector: Unlocks the connector

Threading Model
===============

The sample uses multiple threads for concurrent operation:

* Main thread: Network setup, OCPP initialization, and test scenario control
* OCPP internal thread: Processes OCPP protocol messages
* WebSocket reader thread: Handles incoming WebSocket data
* Per-connector threads (2): Each manages a single connector's charging lifecycle
* ZBUS is used for inter-thread communication (e.g., stop charging events)

Meter Value Simulation
=======================

The sample simulates increasing energy meter readings. In the user notification
callback, when ``OCPP_USR_GET_METER_VALUE`` is invoked:

* Returns simulated Wh (watt-hour) values
* Values increment with each request to simulate energy consumption
* Initial value: 6 Wh + connector number
* Increments by 1 Wh on each meter value request

Sample Output
=============

When running on the STM32F769I Discovery board:

.. code-block:: console

   *** Booting Zephyr OS build v3.6.0-rc1-37-g8c035d8f24cf ***
   OCPP sample stm32f769i_disco
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

Key log messages explained:

* ``net_dhcpv4: Received``: IP address obtained from DHCP server
* ``cs server``: Central System server address resolved
* ``sntp succ since Epoch``: Time synchronized via SNTP
* ``ocpp init success``: OCPP library initialized and connected to Central System
* ``ocpp auth 0> idcon X status 1``: Authorization successful (status 1 = Accepted)
* ``ocpp start charging connector id X``: Charging transaction started
* ``ocpp stop charging connector id X``: Charging transaction stopped

Testing with SteVe
==================

Once the application is running and connected to SteVe:

1. **View Charge Point Status**

   * In SteVe web interface, go to "Home" or "Charge Points"
   * You should see "zephyr" charge point listed as "Available"

2. **Monitor Transactions**

   * Go to "Data Management" → "Transactions"
   * View active and completed charging sessions
   * Check meter values and timestamps

3. **Send Remote Commands**

   * Go to "Operations" → "Charge Point Operations"
   * Select the "zephyr" charge point
   * Try "Remote Start Transaction" or "Remote Stop Transaction"
   * The sample will log the received commands and respond accordingly

4. **View Meter Values**

   * In a transaction, click on "Values" to see the reported meter readings
   * Values should show incremental energy consumption

Troubleshooting
===============

Connection Issues
-----------------

If the application cannot connect to the Central System:

* Verify the server IP address and port in :file:`prj.conf`
* Ensure the SteVe server is running and accessible
* Check network connectivity (ping the server from the network)
* Verify firewall settings allow WebSocket connections on port 8180

Authorization Failures
----------------------

If authorization fails:

* Ensure the Charge Point ID "zephyr" is registered in SteVe
* Check that the Central System is in "Accepted" status
* Verify BootNotification was successful (check SteVe logs)

Time-Related Issues
-------------------

If timestamps are incorrect:

* Verify SNTP server address in :file:`prj.conf`
* Ensure SNTP server is accessible from the network
* Check that :kconfig:option:`CONFIG_SNTP` is enabled
