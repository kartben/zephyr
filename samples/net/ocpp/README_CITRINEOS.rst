.. _ocpp_citrineos:

OCPP CitrineOS Demo
###################

Overview
********

This demo application shows how to connect a Zephyr-based OCPP charge point
to CitrineOS, an open-source OCPP Central System implementation.

CitrineOS (https://github.com/citrineos) is a modern, cloud-native OCPP central
system that supports OCPP 1.6 and OCPP 2.0.1. This demo uses OCPP 1.6 protocol
to establish a WebSocket connection with CitrineOS and demonstrates:

* Charge point registration and boot notification
* ID tag authorization
* Starting and stopping charging transactions
* Meter value reporting
* Remote start/stop operations from the Central System

Requirements
************

Hardware
========

- Linux machine or any network-capable Zephyr board
- Ethernet connection (or configured WiFi)
- STM32 Discovery kit (32F769IDISCOVERY) or similar

Software
========

- Docker and Docker Compose (for running CitrineOS)
- Zephyr development environment

Setting up CitrineOS
*********************

The easiest way to run CitrineOS is using Docker:

1. Clone the CitrineOS repository:

   .. code-block:: console

      git clone https://github.com/citrineos/citrineos-core.git
      cd citrineos-core

2. Start CitrineOS using Docker Compose:

   .. code-block:: console

      docker-compose up -d

   This will start CitrineOS with the default configuration on port 8080.

3. Access the CitrineOS web interface at http://localhost:8080

4. Note your local IP address (the IP where CitrineOS is running):

   .. code-block:: console

      # On Linux
      ip addr show

Building and Running
********************

Build the CitrineOS demo application:

.. zephyr-app-commands::
   :zephyr-app: samples/net/ocpp
   :board: <board to use>
   :gen-args: -DOVERLAY_CONFIG=overlay-citrineos.conf -DCONFIG_NET_SAMPLE_OCPP_SERVER=\"<citrineos-ip>\" -DCONFIG_NET_SAMPLE_SNTP_SERVER=\"<sntp-server-ip>\"
   :goals: build
   :compact:

For example, to build for the STM32F769I Discovery board:

.. code-block:: console

   west build -b stm32f769i_disco samples/net/ocpp -- \
     -DOVERLAY_CONFIG=overlay-citrineos.conf \
     -DCONFIG_NET_SAMPLE_OCPP_SERVER=\"192.168.1.100\" \
     -DCONFIG_NET_SAMPLE_SNTP_SERVER=\"pool.ntp.org\"

Flash the board:

.. code-block:: console

   west flash

Using the Alternative CitrineOS Demo Source
*******************************************

If you want to use the dedicated CitrineOS demo source file (main_citrineos.c),
modify the CMakeLists.txt to use it:

.. code-block:: cmake

   target_sources(app PRIVATE src/main_citrineos.c)

Expected Output
***************

When the application starts, you should see output similar to:

.. code-block:: console

   *** Booting Zephyr OS build v4.3.0 ***
   OCPP CitrineOS Demo stm32f769i_disco
   Connecting to CitrineOS Central System
   [00:00:02.642,000] <inf> net_dhcpv4: Received: 192.168.1.101
   [00:00:02.642,000] <inf> citrineos_demo: Your address: 192.168.1.101
   [00:00:07.011,000] <inf> citrineos_demo: cs server 192.168.1.100 8080
   [00:00:07.024,000] <inf> citrineos_demo: sntp succ since Epoch: 1707890823
   [00:00:07.025,000] <inf> ocpp: ocpp init success
   [00:00:17.066,000] <inf> citrineos_demo: ocpp auth 0> idcon 1 status 1
   [00:00:17.101,000] <inf> citrineos_demo: ocpp auth 0> idcon 2 status 1
   [00:00:17.197,000] <inf> citrineos_demo: ocpp start charging connector id 1
   [00:00:17.255,000] <inf> citrineos_demo: ocpp start charging connector id 2
   [00:01:07.064,000] <inf> citrineos_demo: ocpp stop charging connector id 1
   [00:01:08.063,000] <inf> citrineos_demo: ocpp stop charging connector id 2

Monitoring in CitrineOS
************************

Once connected, you can monitor the charge point in the CitrineOS web interface:

1. Navigate to the Charge Points section
2. Look for the charge point with ID "CitrineOSDemo" or "zephyr"
3. View transaction history, meter values, and send remote commands

Differences from SteVe Configuration
*************************************

The main differences when connecting to CitrineOS vs SteVe are:

* **Port**: CitrineOS uses port 8080 (vs 8180 for SteVe)
* **WebSocket Path**: CitrineOS uses ``/ocpp`` (vs ``/steve/websocket/CentralSystemService/<id>`` for SteVe)
* **Charge Point ID**: The demo uses "CitrineOSDemo" as the model name

Troubleshooting
***************

Connection Issues
=================

If the charge point cannot connect to CitrineOS:

1. Verify CitrineOS is running: ``docker ps``
2. Check firewall rules allow port 8080
3. Ensure the charge point and CitrineOS are on the same network
4. Verify the IP address is correct in the build configuration

Authorization Failures
======================

If ID tag authorization fails:

1. Check CitrineOS logs: ``docker logs citrineos``
2. Verify the ID tag is registered in CitrineOS
3. Some CitrineOS configurations may require pre-registration of ID tags

References
**********

* CitrineOS: https://github.com/citrineos/citrineos-core
* OCPP 1.6 Specification: https://www.openchargealliance.org/protocols/ocpp-16/
* Zephyr OCPP API: :ref:`ocpp_api`
