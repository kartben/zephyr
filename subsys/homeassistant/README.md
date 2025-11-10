# Home Assistant Integration for Zephyr

This new subsystem provides seamless integration between Zephyr RTOS and Home Assistant using MQTT and Zbus.

## Overview

The Home Assistant subsystem makes it incredibly simple to expose sensor data and device states to Home Assistant. Users don't need to write any MQTT code or handle networking details - they simply publish data to Zbus channels, and it automatically appears in Home Assistant!

## Key Features

- **Zero Network Configuration**: No MQTT code needed in your application
- **Zbus Integration**: Uses Zephyr's message bus for decoupled architecture
- **Automatic MQTT Publishing**: Transparently forwards Zbus messages to MQTT
- **Home Assistant Auto-Discovery**: Sensors automatically appear in Home Assistant UI
- **Multiple Sensor Types**: Temperature, humidity, pressure, battery, binary sensors, and more
- **Flexible Configuration**: Configure via Kconfig or runtime API

## Architecture

```
┌─────────────────┐
│ Your App        │
│ (Publishes to   │
│  Zbus channels) │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Zbus            │
│ (Message Bus)   │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Home Assistant  │
│ Subsystem       │
│ (Listener)      │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ MQTT Client     │
│ (Network)       │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Home Assistant  │
│ (MQTT Broker)   │
└─────────────────┘
```

## How to Use

### 1. Enable the Subsystem

In your `prj.conf`:

```kconfig
CONFIG_HOMEASSISTANT=y
CONFIG_HOMEASSISTANT_MQTT_BROKER_HOSTNAME="homeassistant.local"
CONFIG_HOMEASSISTANT_MQTT_BROKER_PORT=1883
CONFIG_HOMEASSISTANT_DEVICE_NAME="my_device"
```

### 2. Define Zbus Channels

```c
#include <zephyr/zbus/zbus.h>

ZBUS_CHAN_DEFINE(temperature_chan, float, NULL, NULL,
                 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0.0f));
```

### 3. Register with Home Assistant

```c
#include <zephyr/homeassistant/homeassistant.h>

static const struct homeassistant_channel_config temp_config = {
    .channel = &temperature_chan,
    .sensor_type = HOMEASSISTANT_SENSOR_TEMPERATURE,
    .friendly_name = "Room Temperature",
    .unit_of_measurement = "°C",
};

void main(void) {
    homeassistant_register_channel(&temp_config);
}
```

### 4. Publish Data (Business as Usual)

```c
float temp = read_temperature();
zbus_chan_pub(&temperature_chan, &temp, K_SECONDS(1));
```

That's it! The temperature now appears in Home Assistant automatically.

## Supported Data Types

The subsystem automatically formats these types for MQTT:
- `float` - Formatted as "%.2f"
- `int32_t` - Formatted as "%d"
- `bool` - Formatted as "ON"/"OFF"

For complex types, you can provide a custom topic and format the data yourself using `homeassistant_publish()`.

## Configuration Options

### Required
- `CONFIG_HOMEASSISTANT=y` - Enable the subsystem
- `CONFIG_HOMEASSISTANT_MQTT_BROKER_HOSTNAME` - MQTT broker address

### Optional
- `CONFIG_HOMEASSISTANT_MQTT_USERNAME` - MQTT authentication username
- `CONFIG_HOMEASSISTANT_MQTT_PASSWORD` - MQTT authentication password
- `CONFIG_HOMEASSISTANT_AUTO_DISCOVERY` - Enable Home Assistant discovery (default: y)
- `CONFIG_HOMEASSISTANT_DEVICE_NAME` - Device name in Home Assistant
- `CONFIG_HOMEASSISTANT_MAX_CHANNELS` - Maximum channels to monitor (default: 16)

## API Reference

### `homeassistant_register_channel()`
Register a Zbus channel for automatic Home Assistant publishing.

### `homeassistant_unregister_channel()`
Unregister a channel.

### `homeassistant_is_connected()`
Check MQTT connection status.

### `homeassistant_publish()`
Manually publish to an MQTT topic (for advanced use cases).

## Sample Application

See `samples/subsys/homeassistant/` for a complete working example that simulates temperature, humidity, and motion sensors.

## Building the Sample

```bash
west build -b <board> samples/subsys/homeassistant
west flash
```

Make sure your board has network connectivity and can reach your MQTT broker.

## Home Assistant Setup

1. Ensure MQTT integration is configured in Home Assistant
2. Make sure the MQTT broker is accessible from your device
3. Sensors will automatically discover when your device connects
4. Find them in Settings → Devices & Services → MQTT

## Benefits

### For Application Developers
- **Simple**: No MQTT code in your app
- **Decoupled**: Use Zbus for clean architecture
- **Flexible**: Works with any Zbus channel

### For System Integration
- **Transparent**: No changes to existing Zbus-based code
- **Scalable**: Register as many channels as needed
- **Reliable**: Built on proven MQTT protocol

## Dependencies

- `CONFIG_ZBUS` (automatically selected)
- `CONFIG_MQTT_LIB` (automatically selected)
- `CONFIG_NET_IPV4` (automatically selected)
- `CONFIG_NET_SOCKETS` (automatically selected)

## Future Enhancements

Potential future additions:
- MQTT Subscribe support (Home Assistant → Zephyr commands)
- TLS/SSL support for secure connections
- QoS configuration per channel
- Custom formatters for complex data types
- Support for Home Assistant controls (switches, buttons, etc.)

## Files

- `subsys/homeassistant/` - Subsystem implementation
- `include/zephyr/homeassistant/` - Public API header
- `samples/subsys/homeassistant/` - Sample application

## License

Apache-2.0
