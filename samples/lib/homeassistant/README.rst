.. zephyr:code-sample:: homeassistant
   :name: Home Assistant Integration
   :relevant-api: homeassistant

   Integrate a Zephyr device with Home Assistant using MQTT discovery.

Overview
********

This sample demonstrates how to use the Home Assistant integration library
to automatically register and update sensors in Home Assistant.

The sample registers a temperature sensor and periodically updates its value.
Home Assistant will automatically discover the sensor through MQTT and
display it in the UI.

Requirements
************

* Network connectivity (Ethernet or WiFi)
* MQTT broker accessible from the device
* Home Assistant instance configured with MQTT integration

Building and Running
********************

Configure your MQTT broker settings in the sample configuration or through
Kconfig. By default, it expects an MQTT broker at ``mqtt://localhost:1883``.

To build and run the sample:

.. zephyr-app-commands::
   :zephyr-app: samples/lib/homeassistant
   :board: native_sim
   :goals: build run
   :compact:

Configuration
*************

The following Kconfig options can be configured:

* ``CONFIG_HOMEASSISTANT_MQTT_BROKER`` - MQTT broker hostname
* ``CONFIG_HOMEASSISTANT_MQTT_PORT`` - MQTT broker port (default 1883)

Expected Output
***************

.. code-block:: console

   *** Booting Zephyr OS build v3.7.0 ***
   [00:00:00.000,000] <inf> homeassistant: Home Assistant client initialized for device: Zephyr Device
   [00:00:00.100,000] <inf> homeassistant: Connected to MQTT broker at localhost:1883
   [00:00:00.200,000] <inf> homeassistant: Registered entity: Temperature Sensor (temp_sensor)
   [00:00:01.000,000] <inf> app: Temperature: 22.5°C
   [00:00:06.000,000] <inf> app: Temperature: 23.1°C

The sensor should now appear in your Home Assistant instance under the
"Zephyr Device" device.
