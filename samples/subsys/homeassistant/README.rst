.. _homeassistant_sample:

Home Assistant Integration Sample
##################################

Overview
********

This sample demonstrates how to integrate Zephyr applications with Home Assistant
using MQTT and Zbus. The integration is designed to be simple and transparent -
you just publish data to Zbus channels, and it automatically appears in Home Assistant!

Features
========

* **Zero Network Configuration**: Users don't need to think about networking at all
* **Zbus Integration**: Simply publish to Zbus channels
* **Automatic MQTT Publishing**: Data is automatically published via MQTT
* **Home Assistant Auto-Discovery**: Sensors automatically appear in Home Assistant
* **Multiple Sensor Types**: Support for temperature, humidity, binary sensors, and more

How It Works
============

1. Define your data as Zbus channels
2. Register channels with the Home Assistant subsystem
3. Publish data to Zbus channels as usual
4. Data automatically appears in Home Assistant via MQTT

Requirements
************

* Network connectivity (Ethernet or WiFi)
* Access to an MQTT broker (typically your Home Assistant instance)
* Home Assistant with MQTT integration enabled

Building and Running
********************

Configure your MQTT broker settings in ``prj.conf``:

.. code-block:: cfg

   CONFIG_HOMEASSISTANT_MQTT_BROKER_HOSTNAME="homeassistant.local"
   CONFIG_HOMEASSISTANT_MQTT_BROKER_PORT=1883
   CONFIG_HOMEASSISTANT_DEVICE_NAME="my_zephyr_device"

Build and flash the sample:

.. code-block:: console

   west build -b <board> samples/subsys/homeassistant
   west flash

The sample will simulate temperature, humidity, and motion sensors. Check your
Home Assistant dashboard to see the sensors appear automatically!

Sample Output
=============

.. code-block:: console

   *** Booting Zephyr OS ***
   [00:00:00.010,000] <inf> ha_sample: =================================================
   [00:00:00.010,000] <inf> ha_sample:   Home Assistant Integration Sample
   [00:00:00.010,000] <inf> ha_sample: =================================================
   [00:00:00.010,000] <inf> ha_sample:
   [00:00:00.010,000] <inf> ha_sample: This sample demonstrates easy Home Assistant
   [00:00:00.010,000] <inf> ha_sample: integration using Zbus and MQTT.
   [00:00:00.010,000] <inf> ha_sample:
   [00:00:00.010,000] <inf> ha_sample: Simply publish to Zbus channels and they
   [00:00:00.010,000] <inf> ha_sample: automatically appear in Home Assistant!
   [00:00:00.010,000] <inf> ha_sample:
   [00:00:00.010,000] <inf> homeassistant: Home Assistant integration starting...
   [00:00:00.050,000] <inf> homeassistant: Connected to MQTT broker
   [00:00:00.100,000] <inf> ha_sample: ✓ Connected to Home Assistant!
   [00:00:05.000,000] <inf> ha_sample: Temperature: 20.5 °C
   [00:00:10.000,000] <inf> ha_sample: Humidity: 52.3 %
   [00:00:15.000,000] <inf> ha_sample: Motion: DETECTED

Usage Example
*************

Here's how simple it is to use in your own application:

.. code-block:: c

   #include <zephyr/homeassistant/homeassistant.h>
   #include <zephyr/zbus/zbus.h>

   /* Define a Zbus channel */
   ZBUS_CHAN_DEFINE(temperature_chan, float, NULL, NULL,
                    ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

   /* Register it with Home Assistant */
   static const struct homeassistant_channel_config temp_config = {
       .channel = &temperature_chan,
       .sensor_type = HOMEASSISTANT_SENSOR_TEMPERATURE,
       .friendly_name = "Room Temperature",
       .unit_of_measurement = "°C",
   };

   void main(void) {
       homeassistant_register_channel(&temp_config);

       /* Now just use Zbus normally - Home Assistant sees the data! */
       float temp = 23.5;
       zbus_chan_pub(&temperature_chan, &temp, K_SECONDS(1));
   }

That's it! No MQTT code needed in your application.

Home Assistant Configuration
*****************************

Make sure MQTT is configured in Home Assistant:

1. Go to Settings → Devices & Services
2. Add MQTT integration if not already present
3. Configure your MQTT broker
4. Sensors will auto-discover when the Zephyr device connects

The sensors will appear under your device name in Home Assistant's MQTT integration.
