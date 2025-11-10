# Quick Start Guide - Home Assistant Integration

## 5-Minute Setup

### Step 1: Enable in prj.conf
```kconfig
CONFIG_HOMEASSISTANT=y
CONFIG_HOMEASSISTANT_MQTT_BROKER_HOSTNAME="192.168.1.100"  # Your Home Assistant IP
CONFIG_HOMEASSISTANT_DEVICE_NAME="my_zephyr_sensor"
```

### Step 2: Create a Zbus Channel
```c
#include <zephyr/zbus/zbus.h>

ZBUS_CHAN_DEFINE(temp_chan, float, NULL, NULL,
                 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0.0f));
```

### Step 3: Register It
```c
#include <zephyr/homeassistant/homeassistant.h>

static const struct homeassistant_channel_config config = {
    .channel = &temp_chan,
    .sensor_type = HOMEASSISTANT_SENSOR_TEMPERATURE,
    .friendly_name = "My Temperature Sensor",
    .unit_of_measurement = "°C",
};

int main(void) {
    homeassistant_register_channel(&config);
    return 0;
}
```

### Step 4: Publish Data
```c
// In your sensor reading code
float temperature = 23.5f;
zbus_chan_pub(&temp_chan, &temperature, K_SECONDS(1));
```

### Step 5: Check Home Assistant
Open Home Assistant → Settings → Devices & Services → MQTT
Your sensor will appear automatically!

## Common Sensor Types

### Temperature
```c
.sensor_type = HOMEASSISTANT_SENSOR_TEMPERATURE,
.unit_of_measurement = "°C",
```

### Humidity
```c
.sensor_type = HOMEASSISTANT_SENSOR_HUMIDITY,
.unit_of_measurement = "%",
```

### Motion (Binary)
```c
.sensor_type = HOMEASSISTANT_SENSOR_BINARY,
// Use bool type for the channel
```

### Battery
```c
.sensor_type = HOMEASSISTANT_SENSOR_BATTERY,
.unit_of_measurement = "%",
```

## Troubleshooting

### "Not connecting to MQTT"
- Check `CONFIG_HOMEASSISTANT_MQTT_BROKER_HOSTNAME` is correct
- Verify network connectivity
- Check firewall allows port 1883
- Enable logs: `CONFIG_HOMEASSISTANT_LOG_LEVEL_DBG=y`

### "Sensor not appearing in Home Assistant"
- Ensure MQTT integration is set up in Home Assistant
- Check the MQTT broker is the same one Home Assistant uses
- Look in Home Assistant → Developer Tools → MQTT for your topics

### "Authentication failed"
```kconfig
CONFIG_HOMEASSISTANT_MQTT_USERNAME="your_username"
CONFIG_HOMEASSISTANT_MQTT_PASSWORD="your_password"
```

## Example: Multiple Sensors

```c
// Define channels
ZBUS_CHAN_DEFINE(temp_chan, float, NULL, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0.0f));
ZBUS_CHAN_DEFINE(humid_chan, float, NULL, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0.0f));
ZBUS_CHAN_DEFINE(motion_chan, bool, NULL, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(false));

// Configure
static const struct homeassistant_channel_config temp_cfg = {
    .channel = &temp_chan,
    .sensor_type = HOMEASSISTANT_SENSOR_TEMPERATURE,
    .friendly_name = "Room Temperature",
    .unit_of_measurement = "°C",
};

static const struct homeassistant_channel_config humid_cfg = {
    .channel = &humid_chan,
    .sensor_type = HOMEASSISTANT_SENSOR_HUMIDITY,
    .friendly_name = "Room Humidity",
    .unit_of_measurement = "%",
};

static const struct homeassistant_channel_config motion_cfg = {
    .channel = &motion_chan,
    .sensor_type = HOMEASSISTANT_SENSOR_BINARY,
    .friendly_name = "Motion Detector",
};

// Register all
homeassistant_register_channel(&temp_cfg);
homeassistant_register_channel(&humid_cfg);
homeassistant_register_channel(&motion_cfg);

// Publish
float temp = 22.5f;
float humid = 65.0f;
bool motion = true;

zbus_chan_pub(&temp_chan, &temp, K_SECONDS(1));
zbus_chan_pub(&humid_chan, &humid, K_SECONDS(1));
zbus_chan_pub(&motion_chan, &motion, K_SECONDS(1));
```

Done! All three sensors now appear in Home Assistant.
