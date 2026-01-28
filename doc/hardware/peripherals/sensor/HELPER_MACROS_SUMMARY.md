# Sensor Driver Helper Macros - Summary

This document provides a quick reference for the sensor driver helper macros added to reduce boilerplate code in Zephyr sensor drivers.

## Quick Reference

### Register Access Macros

#### `SENSOR_I2C_REG_ACCESS(prefix, config_type, i2c_field)`
Generates `prefix_reg_read()` and `prefix_reg_write()` functions for I2C.

**Example:**
```c
SENSOR_I2C_REG_ACCESS(lm75, lm75_config, i2c)
// Generates: lm75_reg_read() and lm75_reg_write()
```

#### `SENSOR_SPI_REG_ACCESS(prefix, config_type, spi_field)`
Generates `prefix_reg_read()` and `prefix_reg_write()` functions for SPI.

**Note:** Uses VLA, keep transfer sizes reasonable (< 64 bytes).

### Initialization Macros

#### `SENSOR_DEVICE_I2C_BUS_READY_CHECK(dev, cfg, i2c_field)`
Checks if I2C bus is ready, logs error and returns -ENODEV if not.

#### `SENSOR_DEVICE_SPI_BUS_READY_CHECK(dev, cfg, spi_field)`
Checks if SPI bus is ready, logs error and returns -ENODEV if not.

#### `SENSOR_DEVICE_PM_INIT(dev)`
Initializes runtime power management (only when CONFIG_PM_DEVICE_RUNTIME is enabled).

### Power Management Macros

#### `SENSOR_PM_ACTION_NOOP(prefix)`
Generates a no-op PM action function that accepts standard PM actions but performs no operations.

**Example:**
```c
SENSOR_PM_ACTION_NOOP(lm75)
// Generates: lm75_pm_action()
```

### Trigger Handling Macros

#### `SENSOR_TRIGGER_GPIO_INIT(...)`
Initializes GPIO trigger (work queue, GPIO pin, callback). See header for full parameter list.

#### `SENSOR_TRIGGER_WORK_HANDLER(prefix, data_type)`
Generates a work handler that calls the user's trigger handler.

#### `SENSOR_TRIGGER_GPIO_CALLBACK(prefix, data_type)`
Generates a GPIO callback that submits work to the work queue.

### Device Instantiation Macros

#### `SENSOR_DEVICE_DT_INST_I2C_DEFINE(...)`
Complete device instantiation for I2C sensors.

**Example:**
```c
#define LM75_INST(inst) \
    SENSOR_DEVICE_DT_INST_I2C_DEFINE(lm75, inst, \
        lm75_data, lm75_config, \
        lm75_init, lm75_pm_action, lm75_driver_api)

DT_INST_FOREACH_STATUS_OKAY(LM75_INST)
```

#### `SENSOR_DEVICE_DT_INST_SPI_DEFINE(...)`
Complete device instantiation for SPI sensors.

#### `SENSOR_DEVICE_DT_INST_I2C_DEFINE_WITH_TRIGGER(...)`
Device instantiation for I2C sensors with GPIO trigger support.

## Typical Usage Pattern

```c
#define DT_DRV_COMPAT example_sensor

#include <zephyr/drivers/sensor/sensor_driver_helpers.h>

/* 1. Define data and config structures */
struct example_data {
    struct sensor_value reading;
};

struct example_config {
    struct i2c_dt_spec i2c;
};

/* 2. Generate register access functions */
SENSOR_I2C_REG_ACCESS(example, example_config, i2c)

/* 3. Implement sensor-specific logic */
static int example_fetch(const struct device *dev) {
    /* ... implementation ... */
}

static int example_sample_fetch(const struct device *dev, enum sensor_channel chan) {
    switch (chan) {
    case SENSOR_CHAN_ALL:
        return example_fetch(dev);
    default:
        return -ENOTSUP;
    }
}

static int example_channel_get(const struct device *dev, enum sensor_channel chan,
                                struct sensor_value *val) {
    struct example_data *data = dev->data;
    switch (chan) {
    case SENSOR_CHAN_AMBIENT_TEMP:
        *val = data->reading;
        return 0;
    default:
        return -ENOTSUP;
    }
}

/* 4. Define API */
static DEVICE_API(sensor, example_api) = {
    .sample_fetch = example_sample_fetch,
    .channel_get = example_channel_get,
};

/* 5. Generate PM handler */
SENSOR_PM_ACTION_NOOP(example)

/* 6. Implement init function using helpers */
static int example_init(const struct device *dev) {
    const struct example_config *cfg = dev->config;
    
    SENSOR_DEVICE_I2C_BUS_READY_CHECK(dev, cfg, i2c);
    SENSOR_DEVICE_PM_INIT(dev);
    
    /* Sensor-specific initialization */
    uint8_t config = 0x01;
    return example_reg_write(dev, 0x02, &config, 1);
}

/* 7. Device instantiation */
#define EXAMPLE_INST(inst) \
    SENSOR_DEVICE_DT_INST_I2C_DEFINE(example, inst, \
        example_data, example_config, \
        example_init, example_pm_action, example_api)

DT_INST_FOREACH_STATUS_OKAY(EXAMPLE_INST)
```

## Lines of Code Saved

For a typical simple sensor driver:

- **Register access**: ~10-15 lines → 1 line
- **Bus ready check**: ~4 lines → 1 line
- **PM initialization**: ~7 lines → 1 line
- **PM handler**: ~13 lines → 1 line
- **Device instantiation**: ~7-10 lines → 1-5 lines

**Total savings**: ~40-50 lines per driver = **~1000+ lines across all sensor drivers**

## Limitations

1. SPI write uses VLA - keep transfers < 64 bytes
2. Some macros cast away const (documented and intentional)
3. Designed for common patterns - complex cases may need custom code
4. Requires specific field names in data structures for trigger macros

## Files

- Header: `include/zephyr/drivers/sensor/sensor_driver_helpers.h`
- Documentation: `doc/hardware/peripherals/sensor/sensor_driver_helpers.rst`
- Example: `drivers/sensor/zephyr_example_sensor.c`
