# DT Doctor Demo - Board Overlays

This directory contains example devicetree overlay files that demonstrate different error scenarios for dtdoctor.

## Available Overlays

### disabled_sensor.overlay
Demonstrates the "disabled devicetree node" scenario.

- Defines a sensor node (`demo_sensor`) attached to I2C
- Sets the sensor status to `"disabled"`
- When used with `main_error.c`, causes a compile error that dtdoctor diagnoses

**Usage:**
```bash
# Copy to your board's overlay
cp boards/disabled_sensor.overlay ../../boards/<your_board>.overlay

# Or use as DTC_OVERLAY_FILE
west build -b <board> -- -DDTC_OVERLAY_FILE=samples/dtdoctor_demo/boards/disabled_sensor.overlay
```

### missing_driver.overlay
Demonstrates the "enabled node without driver" scenario.

- Defines a sensor node (`demo_sensor`) attached to I2C
- Sets the sensor status to `"okay"` (enabled)
- When built without CONFIG_BME280, causes a linker error that dtdoctor diagnoses

**Usage:**
```bash
# Copy to your board's overlay
cp boards/missing_driver.overlay ../../boards/<your_board>.overlay

# Or use as DTC_OVERLAY_FILE
west build -b <board> -- -DDTC_OVERLAY_FILE=samples/dtdoctor_demo/boards/missing_driver.overlay
```

## Notes

- Both overlays assume your board has an I2C controller labeled `i2c0`
- If your board uses a different I2C controller, modify the overlay accordingly
- The overlays use the `bosch,bme280` compatible as an example - a common sensor in Zephyr
- These are for demonstration purposes only and may need adaptation for your specific board

## Adapting for Your Board

If `&i2c0` doesn't exist on your board, find the correct I2C node:

```bash
# Build any sample to generate devicetree
west build -b <your_board> samples/hello_world

# Look at the devicetree
cat build/zephyr/zephyr.dts | grep -A 5 "i2c@"
```

Then update the overlay to reference the correct I2C node (e.g., `&i2c1`, `&i2c2`, etc.).
