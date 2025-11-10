.. zephyr:code-sample:: homeassistant-basic
   :name: Home Assistant Integration Basic
   :relevant-api: homeassistant_api zbus_apis

   Integrate Zephyr sensors with Home Assistant using Zbus channels.

Overview
********

This sample demonstrates how to integrate Zephyr-based IoT devices with Home Assistant
using the Home Assistant integration library. The library bridges Zbus channels to
Home Assistant, allowing developers to expose sensor data and control entities without
writing any networking code.

The sample creates three entities:

* **Temperature sensor** - Reports temperature in Celsius
* **Humidity sensor** - Reports humidity percentage
* **Light switch** - Binary switch that can be toggled

All sensor data is published through Zbus channels and automatically synchronized with
Home Assistant via the REST API.

Requirements
************

* Home Assistant instance running and accessible on the network
* Long-lived access token from Home Assistant (create from your profile)
* Network connectivity (Ethernet or Wi-Fi configured)

Configuration
*************

Before building, update the following configurations in ``prj.conf``:

.. code-block:: cfg

   CONFIG_HOMEASSISTANT_SERVER_ADDR="192.168.1.100"  # Your Home Assistant IP
   CONFIG_HOMEASSISTANT_SERVER_PORT=8123              # Home Assistant port
   CONFIG_HOMEASSISTANT_API_TOKEN="your_token_here"  # Your API token
   CONFIG_NET_CONFIG_MY_IPV4_ADDR="192.168.1.10"     # Device IP address

To generate a Home Assistant API token:

1. Log into your Home Assistant web interface
2. Click on your profile (bottom left)
3. Scroll to "Long-Lived Access Tokens"
4. Click "Create Token"
5. Copy the token and update ``CONFIG_HOMEASSISTANT_API_TOKEN``

Building and Running
********************

This sample can be built for any board with networking support. Example for native_sim:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/homeassistant/basic
   :host-os: unix
   :board: native_sim
   :goals: build
   :compact:

Or for a real board with Ethernet (e.g., STM32 Nucleo):

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/homeassistant/basic
   :host-os: unix
   :board: nucleo_f767zi
   :goals: build flash
   :compact:

Sample Output
=============

.. code-block:: console

   *** Booting Zephyr OS build v3.x.x ***
   [00:00:00.000,000] <inf> ha_sample: Home Assistant Integration Sample
   [00:00:00.000,000] <inf> ha_sample: ===================================
   [00:00:00.000,000] <inf> ha_sample: This sample demonstrates Zbus integration with Home Assistant
   [00:00:00.000,000] <inf> ha_sample: Sensor data is automatically published to Home Assistant
   [00:00:00.000,000] <inf> ha_sample:
   [00:00:00.000,000] <inf> ha_sample: Configuration:
   [00:00:00.000,000] <inf> ha_sample:   Server: 192.168.1.100:8123
   [00:00:00.000,000] <inf> ha_sample:   Update interval: 5000 ms
   [00:00:00.000,000] <inf> homeassistant: Registered entity: temperature (type=0)
   [00:00:00.000,000] <inf> homeassistant: Registered entity: humidity (type=0)
   [00:00:00.000,000] <inf> homeassistant: Registered entity: light (type=2)
   [00:00:00.000,000] <inf> homeassistant: Home Assistant integration initialized
   [00:00:00.000,000] <inf> homeassistant: Home Assistant integration started
   [00:00:00.000,000] <inf> homeassistant: Server: 192.168.1.100:8123
   [00:00:02.000,000] <inf> ha_sample: Starting sensor simulation...
   [00:00:02.000,000] <inf> ha_sample: Temperature: 20°C
   [00:00:02.000,000] <inf> ha_sample: Humidity: 40%
   [00:00:02.000,000] <inf> ha_sample: Light switch: OFF

Home Assistant Integration
***************************

Once the sample is running, the entities will appear in Home Assistant:

* ``sensor.temperature`` - Temperature sensor with °C unit
* ``sensor.humidity`` - Humidity sensor with % unit
* ``sensor.light`` - Light switch control

You can view these entities in Home Assistant's Developer Tools > States page.

How It Works
************

The sample demonstrates the simplicity of the Home Assistant integration:

1. **Define Zbus channels** - Create channels for your sensor data:

   .. code-block:: c

      ZBUS_CHAN_DEFINE(temperature_chan, struct temperature_msg, ...);

2. **Register with Home Assistant** - Use the convenient macro:

   .. code-block:: c

      HOMEASSISTANT_ENTITY_DEFINE(temperature,
                                  HOMEASSISTANT_ENTITY_SENSOR,
                                  "°C",
                                  "temperature",
                                  temperature_chan);

3. **Publish data to Zbus** - The library handles the rest:

   .. code-block:: c

      struct temperature_msg temp = { .value = 25 };
      zbus_chan_pub(&temperature_chan, &temp, K_NO_WAIT);

The Home Assistant integration library automatically:

* Monitors registered Zbus channels for updates
* Formats data as JSON payloads
* Sends HTTP POST requests to Home Assistant REST API
* Handles authentication with your API token
* Updates entities at the configured interval

No networking code needed in your application!
