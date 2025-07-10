# Copyright: (c)  2025, Intel Corporation
# Example usage of PPK2 Power Monitor

import time
import logging
from ppk2.PPK2PowerMonitor import PPK2PowerMonitor

# Set up logging
logging.basicConfig(level=logging.INFO)

def main():
    # Create PPK2 power monitor instance
    ppk2_monitor = PPK2PowerMonitor()

    # Get available devices
    devices = ppk2_monitor.get_available_devices()
    if not devices:
        print("No PPK2 devices found")
        return

    print(f"Found {len(devices)} PPK2 device(s):")
    for port, serial in devices:
        print(f"  Port: {port}, Serial: {serial}")

    # Use the first available device
    device_port = devices[0][0]
    print(f"Using device on port: {device_port}")

    try:
        # Connect to the device
        ppk2_monitor.connect(device_port)

        # Configure the device
        ppk2_monitor.set_source_voltage(3300)  # 3.3V
        ppk2_monitor.set_measurement_mode('AMPERE_MODE')

        # Perform a measurement
        print("Starting measurement...")
        measurement_duration = 5  # 5 seconds
        current_data = ppk2_monitor.get_data(measurement_duration)

        if current_data:
            print(f"Measurement completed. Collected {len(current_data)} samples:")
            print(f"Average current: {sum(current_data)/len(current_data):.6f} A")
            print(f"Min current: {min(current_data):.6f} A")
            print(f"Max current: {max(current_data):.6f} A")
        else:
            print("No measurement data collected")

    except Exception as e:
        print(f"Error: {e}")
    finally:
        # Disconnect
        ppk2_monitor.disconnect()
        print("PPK2 disconnected")

if __name__ == "__main__":
    main()