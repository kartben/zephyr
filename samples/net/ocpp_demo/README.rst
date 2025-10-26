.. zephyr:code-sample:: ocpp-demo
   :name: OCPP EV Charging Station Demo
   :relevant-api: ocpp_api

   Comprehensive OCPP charge point demo with realistic EV charging simulation.

Overview
********

This OCPP demo application showcases a complete Electric Vehicle (EV) charging 
station implementation using the Zephyr OCPP library. It connects to a public 
OCPP Central System server and simulates realistic charging scenarios.

Features
========

* **Dual Connectors**: 
  
  - Connector 1: Fast charging (22 kW)
  - Connector 2: Normal charging (7.4 kW)

* **Realistic Charging Simulation**:
  
  - Dynamic power delivery with gradual ramp-up
  - Real-time meter value reporting (energy, power, current, voltage)
  - Temperature simulation during charging
  - State of Charge (SoC) progression from 20% to 95%
  - Automatic charge termination at 95% SoC

* **OCPP 1.6 Core Profile**:
  
  - Boot Notification
  - Authorization (IdTag validation)
  - Start/Stop Transaction
  - Meter Values reporting
  - Heartbeat
  - Remote Start/Stop Transaction
  - Unlock Connector

* **Comprehensive Logging**:
  
  - Visual status indicators with emojis
  - Real-time charging statistics
  - Energy consumption tracking
  - Session summaries

Connection Details
******************

The demo connects to the public OCPP test server:

* **Server**: cs.ocpp-css.com
* **Port**: 80 (WebSocket over HTTP)
* **Protocol**: OCPP 1.6-J (JSON over WebSocket)

This is a publicly accessible OCPP Central System for testing and development.

Requirements
************

- Network-capable board (Ethernet or WiFi)
- Zephyr RTOS v3.6 or later
- Network connectivity (LAN/WiFi)
- SNTP server access for time synchronization

Supported Boards
================

- STM32 Discovery kit (32F769IDISCOVERY)
- Any board with Ethernet or WiFi support
- QEMU (for testing with TAP networking)

Building and Running
********************

Native Simulation
=================

Build the demo for native simulation:

.. code-block:: console

   west build -b native_sim samples/net/ocpp_demo

Hardware Build
==============

Build for STM32F769I Discovery:

.. code-block:: console

   west build -b stm32f769i_disco samples/net/ocpp_demo
   west flash

Build for other boards:

.. code-block:: console

   west build -b <your_board> samples/net/ocpp_demo

Expected Output
***************

The demo will display a visual interface with status updates:

.. code-block:: console

   ╔═══════════════════════════════════════════════════════╗
   ║   OCPP EV Charging Station Demo                      ║
   ║   Platform: native_sim                                ║
   ║   Connectors: 2 (Fast + Normal)                      ║
   ╚═══════════════════════════════════════════════════════╝

   [00:00:02.100,000] <inf> main: 🌐 Waiting for network connection...
   [00:00:02.642,000] <inf> main: ✅ Network connected!
   [00:00:02.642,000] <inf> main: 🔍 Resolving OCPP server: cs.ocpp-css.com:80
   [00:00:02.850,000] <inf> main: ✅ Server resolved to: 185.215.227.122
   [00:00:02.850,000] <inf> main: 🕐 Synchronizing time via SNTP...
   [00:00:03.200,000] <inf> main: ✅ Time synchronized!
   [00:00:03.201,000] <inf> main: 🔧 Initializing OCPP stack...
   [00:00:03.201,000] <inf> main: ✅ OCPP stack initialized!
   [00:00:03.202,000] <inf> main: 🚀 Starting charging demo scenario...
   
   [00:00:03.202,000] <inf> main: 🚗 Vehicle 1 arriving at connector 1
   [00:00:03.202,000] <inf> main:    IdTag: DemoTag01 | Charge type: FAST
   [00:00:08.205,000] <inf> main: 🔑 Authorizing IdTag 'DemoTag01' for connector 1...
   [00:00:08.450,000] <inf> main: ✅ Authorization successful for connector 1
   [00:00:08.500,000] <inf> main: ⚡ CHARGING STARTED on connector 1 (FAST mode - 22.0 kW)
   
   [00:00:13.500,000] <inf> main: 📊 Connector 1 Status: 10.5 kW | 45.7 A | 58 Wh | 22% SoC | 29.2°C | 5s
   [00:00:18.500,000] <inf> main: 📊 Connector 1 Status: 18.0 kW | 78.3 A | 208 Wh | 25% SoC | 35.8°C | 10s
   [00:00:23.500,000] <inf> main: 📊 Connector 1 Status: 22.0 kW | 95.7 A | 514 Wh | 28% SoC | 41.4°C | 15s
   
   [00:01:03.500,000] <inf> main: 🔋 Connector 1: Battery full (95% SoC), stopping charge
   [00:01:03.650,000] <inf> main: ✅ CHARGING COMPLETED on connector 1
   [00:01:03.650,000] <inf> main:    📈 Final Stats:
   [00:01:03.650,000] <inf> main:       Energy delivered: 1320 Wh
   [00:01:03.650,000] <inf> main:       Charging time: 60 seconds
   [00:01:03.650,000] <inf> main:       Final SoC: 95%
   [00:01:03.650,000] <inf> main:       Avg power: 22.0 kW

Demo Scenario
*************

The demo runs the following scenario:

1. **Initialization**: 
   
   - Connects to network
   - Resolves OCPP server address
   - Synchronizes time via SNTP
   - Initializes OCPP stack

2. **Vehicle Arrivals**:
   
   - Two vehicles arrive at different times
   - Each uses a unique IdTag for authorization
   - Connector 1: Fast charging (22 kW)
   - Connector 2: Normal charging (7.4 kW)

3. **Charging Sessions**:
   
   - Authorization with Central System
   - Transaction start with initial meter reading
   - Dynamic power delivery with gradual ramp-up
   - Periodic meter value updates (every 5 seconds)
   - Real-time status monitoring

4. **Session Completion**:
   
   - Automatic stop at 95% State of Charge
   - Transaction stop with final meter reading
   - Session statistics summary

5. **Remote Operations**:
   
   - Station remains connected for remote commands
   - Supports Remote Start/Stop Transaction
   - Supports Unlock Connector

Interactive Features
********************

From the Central System interface (cs.ocpp-css.com), you can:

* Monitor real-time charging status
* View meter values (energy, power, current, voltage, temperature, SoC)
* Remotely start/stop charging sessions
* Send unlock connector commands
* View transaction history
* Monitor heartbeat messages

Testing with OCPP Central System
*********************************

1. Access the OCPP Central System web interface at https://cs.ocpp-css.com/

2. Look for your charge point in the connected devices list
   (Identifier: "ZephyrCharger_v2")

3. Monitor the charging sessions and meter values

4. Try remote operations:
   
   - Remote Start Transaction
   - Remote Stop Transaction
   - Unlock Connector

Customization
*************

You can customize the demo by modifying these parameters in ``src/main.c``:

* Number of connectors: ``NO_OF_CONN``
* Fast charge power: ``target_power_kw`` (22 kW default)
* Normal charge power: ``target_power_kw`` (7.4 kW default)
* Initial State of Charge: ``soc_percent`` (20% default)
* Charging duration: Adjust sleep timers in ``main()``
* Charge point info: Modify ``ocpp_cp_info`` structure

Configuration Options
*********************

Key configuration options in ``prj.conf``:

* ``CONFIG_NET_SAMPLE_OCPP_SERVER``: OCPP server hostname
* ``CONFIG_NET_SAMPLE_OCPP_PORT``: OCPP server port
* ``CONFIG_NET_SAMPLE_SNTP_SERVER``: SNTP server for time sync
* ``CONFIG_HEAP_MEM_POOL_SIZE``: Heap size (20000 bytes for demo)

Troubleshooting
***************

Network Issues
==============

If the demo fails to connect:

1. Verify network connectivity
2. Check DNS resolution is working
3. Verify firewall allows outbound WebSocket connections
4. Try pinging cs.ocpp-css.com

OCPP Issues
===========

If OCPP operations fail:

1. Check SNTP time synchronization
2. Verify charge point appears on Central System
3. Check IdTag is authorized on the server
4. Enable debug logging: ``CONFIG_LOG_DEFAULT_LEVEL=4``

See Also
********

* :ref:`ocpp_interface` - OCPP API documentation
* :zephyr_file:`samples/net/ocpp` - Basic OCPP sample
* `OCPP 1.6 Specification <https://www.openchargealliance.org/protocols/ocpp-16/>`_
* `cs.ocpp-css.com <https://cs.ocpp-css.com/>`_ - Test Central System
