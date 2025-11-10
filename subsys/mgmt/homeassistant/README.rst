Home Assistant Integration
##########################

The Home Assistant integration subsystem provides seamless integration between
Zephyr-based IoT devices and Home Assistant smart home platform. It bridges Zbus
channels to Home Assistant entities, allowing developers to expose sensor data and
control entities without writing any networking code.

Features
********

* **Zero networking code** - Just publish to Zbus channels
* **Automatic synchronization** - Library handles all communication with Home Assistant
* **Multiple entity types** - Support for sensors, binary sensors, switches, and numbers
* **REST API integration** - Uses Home Assistant's REST API for state updates
* **Easy configuration** - Simple Kconfig options for server and authentication
* **Type-safe API** - Compile-time registration of entities

Architecture
************

The Home Assistant integration consists of:

1. **Entity Registration** - Register Zbus channels as Home Assistant entities
2. **Monitoring Thread** - Background thread that monitors registered channels
3. **HTTP Client** - Sends state updates to Home Assistant via REST API
4. **Authentication** - Handles API token authentication automatically

Usage
*****

Basic usage involves three steps:

1. Define a Zbus channel for your data:

   .. code-block:: c

      struct sensor_data {
          int value;
      };
      
      ZBUS_CHAN_DEFINE(sensor_chan, struct sensor_data, NULL, NULL,
                       ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(.value = 0));

2. Register the channel with Home Assistant:

   .. code-block:: c

      HOMEASSISTANT_ENTITY_DEFINE(my_sensor,
                                  HOMEASSISTANT_ENTITY_SENSOR,
                                  "°C",
                                  "temperature",
                                  sensor_chan);

3. Publish data to the Zbus channel:

   .. code-block:: c

      struct sensor_data data = { .value = 25 };
      zbus_chan_pub(&sensor_chan, &data, K_NO_WAIT);

The library automatically sends updates to Home Assistant at the configured interval.

Configuration
*************

Enable the subsystem in your ``prj.conf``:

.. code-block:: cfg

   CONFIG_HOMEASSISTANT=y
   CONFIG_HOMEASSISTANT_SERVER_ADDR="192.168.1.100"
   CONFIG_HOMEASSISTANT_SERVER_PORT=8123
   CONFIG_HOMEASSISTANT_API_TOKEN="your_long_lived_access_token"

Required dependencies:

.. code-block:: cfg

   CONFIG_ZBUS=y
   CONFIG_NETWORKING=y
   CONFIG_NET_TCP=y
   CONFIG_NET_SOCKETS=y
   CONFIG_HTTP_CLIENT=y

Entity Types
************

The following Home Assistant entity types are supported:

* ``HOMEASSISTANT_ENTITY_SENSOR`` - Read-only sensor (temperature, humidity, etc.)
* ``HOMEASSISTANT_ENTITY_BINARY_SENSOR`` - Binary state sensor (door, motion, etc.)
* ``HOMEASSISTANT_ENTITY_SWITCH`` - Controllable on/off switch
* ``HOMEASSISTANT_ENTITY_NUMBER`` - Numeric value that can be set

API Reference
*************

See ``include/zephyr/mgmt/homeassistant/homeassistant.h`` for complete API documentation.

Key functions:

* ``homeassistant_register_entity()`` - Manually register an entity
* ``homeassistant_init()`` - Initialize the subsystem (called automatically)
* ``HOMEASSISTANT_ENTITY_DEFINE()`` - Macro to define and register an entity

Limitations
***********

Current limitations:

* Maximum 16 entities per device (configurable in source)
* State updates only (no command reception from Home Assistant)
* Assumes simple data types (int) in Zbus messages
* No MQTT support (REST API only)

Future enhancements may include:

* MQTT discovery protocol support
* Bidirectional control (receive commands from Home Assistant)
* Configurable data type conversion callbacks
* TLS/HTTPS support
* Entity auto-discovery

Samples
*******

See ``samples/subsys/homeassistant/basic`` for a complete example demonstrating
temperature, humidity sensors and a light switch.
