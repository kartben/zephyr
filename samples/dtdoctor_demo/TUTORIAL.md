# DT Doctor Demo Tutorial

This tutorial walks you through using the dtdoctor demo to see how dtdoctor helps diagnose Devicetree-related build errors.

## Prerequisites

- Zephyr development environment set up
- A supported board (e.g., reel_board, nrf52840dk, nucleo_f746zg)
- Basic familiarity with west build commands

## Scenario 1: Successfully Building the Demo

First, let's verify the demo builds successfully in its default configuration:

```bash
west build -b reel_board samples/dtdoctor_demo
```

This should build successfully and show that the sample can handle missing devicetree nodes gracefully using conditional compilation.

## Scenario 2: Disabled Devicetree Node Error

Now, let's demonstrate how dtdoctor helps diagnose a disabled devicetree node.

### Step 1: Prepare the error scenario

Copy the disabled sensor overlay to your board's overlay file:

```bash
cp samples/dtdoctor_demo/boards/disabled_sensor.overlay boards/reel_board.overlay
```

### Step 2: Use the error demonstration code

Rename the error demonstration file:

```bash
cd samples/dtdoctor_demo
mv src/main.c src/main_safe.c
mv src/main_error.c src/main.c
```

### Step 3: Build WITHOUT dtdoctor (to see the raw error)

```bash
west build -b reel_board samples/dtdoctor_demo --pristine
```

You should see a cryptic error like:
```
error: '__device_dts_ord_123' undeclared here (not in a function)
```

### Step 4: Build WITH dtdoctor (to see the diagnosis)

```bash
west build -b reel_board samples/dtdoctor_demo --pristine -- -DZEPHYR_SCA_VARIANT=dtdoctor
```

Now dtdoctor will intercept the error and provide helpful diagnostic information:
```
+-------------------------------------------------------------------+
| DT Doctor                                                         |
+===================================================================+
| 'demo_sensor: /soc/i2c@40003000/sensor@40' is disabled in        |
| boards/reel_board.overlay:20                                      |
|                                                                   |
| It is referenced as a "chosen" in 'demo-sensor'                  |
|                                                                   |
| Try enabling the node by setting its 'status' property to        |
| 'okay'.                                                           |
+-------------------------------------------------------------------+
```

### Step 5: Fix the error

Following dtdoctor's advice, edit the overlay file to change `status = "disabled"` to `status = "okay"`.

## Scenario 3: Missing Driver Kconfig Error

This scenario shows how dtdoctor helps when a devicetree node is enabled but the driver is missing.

### Step 1: Use the missing driver overlay

```bash
cp samples/dtdoctor_demo/boards/missing_driver.overlay boards/reel_board.overlay
```

### Step 2: Build with dtdoctor

```bash
west build -b reel_board samples/dtdoctor_demo --pristine -- -DZEPHYR_SCA_VARIANT=dtdoctor
```

You'll see a linker error with dtdoctor's diagnosis:
```
+-------------------------------------------------------------------+
| DT Doctor                                                         |
+===================================================================+
| 'demo_sensor: /soc/i2c@40003000/sensor@40' is enabled but no     |
| driver appears to be available for it.                           |
|                                                                   |
| Try enabling these Kconfig options:                              |
|  - CONFIG_BME280=y                                               |
|  - CONFIG_I2C=y                                                  |
+-------------------------------------------------------------------+
```

### Step 3: Fix the error

Add the suggested Kconfig options to `prj.conf`:

```
CONFIG_GPIO=y
CONFIG_I2C=y
CONFIG_BME280=y
```

Then rebuild - it should succeed!

## Cleanup

To restore the demo to its original state:

```bash
cd samples/dtdoctor_demo
mv src/main.c src/main_error.c
mv src/main_safe.c src/main.c
rm ../../boards/reel_board.overlay  # or restore your original overlay
git checkout prj.conf  # restore original prj.conf
```

## Key Takeaways

1. **dtdoctor saves time**: Instead of puzzling over cryptic `__device_dts_ord_*` symbols, you get actionable diagnostics
2. **Two main scenarios**: Disabled nodes and missing drivers are the most common Devicetree errors
3. **Easy to use**: Just add `-DZEPHYR_SCA_VARIANT=dtdoctor` to your build command
4. **Actionable advice**: dtdoctor tells you exactly what to fix and how

## Next Steps

- Try dtdoctor on your own projects when you encounter Devicetree errors
- Read the full documentation: https://docs.zephyrproject.org/latest/develop/sca/dtdoctor.html
- Use dtdoctor as a learning tool to better understand Devicetree and Kconfig relationships
