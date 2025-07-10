# Copyright: (c)  2025, Intel Corporation
# Author: Kartik Ben <kartik.ben@intel.com>

import logging
import time
import threading
from typing import List, Optional

from abstract.PowerMonitor import PowerMonitor
from ppk2_api.ppk2_api import PPK2_API, PPK2_Modes


class PPK2PowerMonitor(PowerMonitor):
    def __init__(self):
        """
        Initializes the PPK2 Power Monitor.
        """
        self.ppk2 = None
        self.measurement_data = []
        self.is_measuring = False
        self.measurement_thread = None
        self.measurement_stop_event = threading.Event()
        self.source_voltage = 3300  # Default 3.3V
        self.measurement_mode = PPK2_Modes.AMPERE_MODE

    def init(self, device_id: str):
        """
        Initialize the PPK2 power monitor.

        Args:
            device_id (str): Serial port where PPK2 is connected

        Returns:
            bool: True if initialization successful, False otherwise
        """
        try:
            # Initialize PPK2 connection
            self.ppk2 = PPK2_API(device_id, timeout=1, write_timeout=1, exclusive=True)

            # Get device modifiers (calibration data)
            self.ppk2.get_modifiers()

            # Set source voltage
            self.ppk2.set_source_voltage(self.source_voltage)

            # Set measurement mode
            if self.measurement_mode == PPK2_Modes.SOURCE_MODE:
                self.ppk2.use_source_meter()
                self.ppk2.toggle_DUT_power("ON")
            else:
                self.ppk2.use_ampere_meter()

            logging.info(f"PPK2 initialized successfully on port {device_id}")
            return True

        except Exception as e:
            logging.error(f"Failed to initialize PPK2: {e}")
            return False

    def connect(self, power_device_path: str):
        """
        Opens the connection to the PPK2 device.

        Args:
            power_device_path (str): Serial port where PPK2 is connected
        """
        try:
            self.init(power_device_path)
        except Exception as e:
            logging.error(f"Failed to connect to PPK2: {e}")
            raise

    def disconnect(self):
        """
        Closes the connection to the PPK2 device.
        """
        try:
            if self.ppk2:
                if self.measurement_mode == PPK2_Modes.SOURCE_MODE:
                    self.ppk2.toggle_DUT_power("OFF")
                self.ppk2.stop_measuring()
                del self.ppk2
                self.ppk2 = None
            logging.info("PPK2 disconnected successfully")
        except Exception as e:
            logging.error(f"Error disconnecting PPK2: {e}")

    def measure(self, duration: int):
        """
        Start measuring current for the specified duration.

        Args:
            duration (int): The duration of the measurement in seconds.
        """
        if not self.ppk2:
            logging.error("PPK2 not initialized")
            return

        try:
            self.measurement_data = []
            self.is_measuring = True
            self.measurement_stop_event.clear()

            # Start measuring
            self.ppk2.start_measuring()

            # Start measurement thread
            self.measurement_thread = threading.Thread(
                target=self._measurement_worker,
                args=(duration,)
            )
            self.measurement_thread.start()

            logging.info(f"Started PPK2 measurement for {duration} seconds")

        except Exception as e:
            logging.error(f"Failed to start measurement: {e}")
            self.is_measuring = False

    def get_data(self, duration: int) -> List[float]:
        """
        Measure current with specified measurement time.

        Args:
            duration (int): The duration of the measurement in seconds.

        Returns:
            List[float]: An array of measured current values in amperes.
        """
        if not self.ppk2:
            logging.error("PPK2 not initialized")
            return []

        try:
            self.measure(duration)

            # Wait for measurement to complete
            if self.measurement_thread:
                self.measurement_thread.join()

            # Convert from microamperes to amperes
            current_data = [sample / 1_000_000 for sample in self.measurement_data]

            logging.info(f"Completed measurement: {len(current_data)} samples")
            return current_data

        except Exception as e:
            logging.error(f"Failed to get measurement data: {e}")
            return []

    def _measurement_worker(self, duration: int):
        """
        Worker thread for collecting measurement data.

        Args:
            duration (int): Measurement duration in seconds
        """
        start_time = time.time()

        try:
            while time.time() - start_time < duration and not self.measurement_stop_event.is_set():
                # Read data from PPK2
                read_data = self.ppk2.get_data()

                if read_data != b'':
                    samples, _ = self.ppk2.get_samples(read_data)
                    if samples:
                        # Calculate average current for this batch
                        avg_current = sum(samples) / len(samples)
                        self.measurement_data.append(avg_current)

                time.sleep(0.01)  # 10ms delay between reads

        except Exception as e:
            logging.error(f"Error in measurement worker: {e}")
        finally:
            self.is_measuring = False
            if self.ppk2:
                self.ppk2.stop_measuring()

    def set_source_voltage(self, voltage_mv: int):
        """
        Set the source voltage for the PPK2.

        Args:
            voltage_mv (int): Voltage in millivolts (800-5000)
        """
        self.source_voltage = voltage_mv
        if self.ppk2:
            self.ppk2.set_source_voltage(voltage_mv)

    def set_measurement_mode(self, mode: str):
        """
        Set the measurement mode for the PPK2.

        Args:
            mode (str): Either 'AMPERE_MODE' or 'SOURCE_MODE'
        """
        self.measurement_mode = mode
        if self.ppk2:
            if mode == PPK2_Modes.SOURCE_MODE:
                self.ppk2.use_source_meter()
                self.ppk2.toggle_DUT_power("ON")
            else:
                self.ppk2.use_ampere_meter()

    def get_available_devices(self) -> List[tuple]:
        """
        Get list of available PPK2 devices.

        Returns:
            List[tuple]: List of (port, serial_number) tuples
        """
        return PPK2_API.list_devices()

    def stop_measurement(self):
        """
        Stop the current measurement.
        """
        self.measurement_stop_event.set()
        if self.measurement_thread:
            self.measurement_thread.join()
        if self.ppk2:
            self.ppk2.stop_measuring()
        self.is_measuring = False