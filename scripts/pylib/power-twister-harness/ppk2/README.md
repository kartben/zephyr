# PPK2 Power Monitor Implementation

This module provides a PPK2 (Power Profiler Kit II) implementation for the Zephyr power twister harness.

## Overview

The PPK2 Power Monitor is a Python implementation that interfaces with Nordic Semiconductor's Power Profiler Kit II (PPK2) device for power measurements. It follows the same interface as other power monitors in the harness, making it compatible with existing test frameworks.

## Features

- **Ampere Mode**: Measures current consumption of the device under test (DUT)
- **Source Mode**: Provides power to the DUT and measures current consumption
- **Configurable Voltage**: Set source voltage from 800mV to 5000mV
- **High Sampling Rate**: Up to 100 samples per millisecond
- **Thread-safe**: Supports concurrent measurements
- **Device Discovery**: Automatic detection of connected PPK2 devices

## Requirements

- PPK2 device connected via USB
- Python 3.7+
- `ppk2-api-python` package
- `pyserial` package

## Installation

1. Install the PPK2 API:
   ```bash
   pip install ppk2-api-python
   ```

2. Ensure the PPK2 device is connected and recognized by your system.

## Usage

### Basic Usage

```python
from ppk2.PPK2PowerMonitor import PPK2PowerMonitor

# Create monitor instance
ppk2_monitor = PPK2PowerMonitor()

# Get available devices
devices = ppk2_monitor.get_available_devices()
device_port = devices[0][0]  # Use first available device

# Connect and configure
ppk2_monitor.connect(device_port)
ppk2_monitor.set_source_voltage(3300)  # 3.3V
ppk2_monitor.set_measurement_mode('AMPERE_MODE')

# Perform measurement
current_data = ppk2_monitor.get_data(5)  # 5 seconds

# Disconnect
ppk2_monitor.disconnect()
```

### Integration with Twister Harness

To use PPK2 with the twister harness, specify `ppk2` as the probe type in your test configuration:

```yaml
# In your test configuration
fixtures:
  - pm_probe:ppk2:/dev/ttyACM0  # Device path
```

### Available Methods

- `connect(device_path)`: Connect to PPK2 device
- `disconnect()`: Disconnect from device
- `get_data(duration)`: Measure current for specified duration
- `set_source_voltage(voltage_mv)`: Set source voltage (800-5000mV)
- `set_measurement_mode(mode)`: Set mode ('AMPERE_MODE' or 'SOURCE_MODE')
- `get_available_devices()`: List connected PPK2 devices
- `stop_measurement()`: Stop ongoing measurement

## Measurement Modes

### Ampere Mode
- Measures current consumption of the DUT
- DUT must be powered externally
- Use when DUT has its own power supply

### Source Mode
- Provides power to the DUT
- Measures current consumption
- Use when PPK2 should power the DUT

## Data Format

The `get_data()` method returns a list of current values in amperes (A). The PPK2 internally measures in microamperes (μA) and converts to amperes for consistency with other power monitors.

## Error Handling

The implementation includes comprehensive error handling:
- Connection failures
- Device initialization errors
- Measurement timeouts
- Data processing errors

All errors are logged with appropriate error messages.

## Example

See `example_ppk2.py` for a complete working example.

## Troubleshooting

1. **Device not found**: Ensure PPK2 is connected and drivers are installed
2. **Permission denied**: Check USB permissions for the serial port
3. **Measurement failures**: Verify device is properly connected and configured
4. **No data**: Check if DUT is powered and consuming current

## License

Copyright (c) 2025, Intel Corporation