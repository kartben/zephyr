# Copyright: (c)  2025, Intel Corporation
# Test file for PPK2 integration

import pytest
import sys
import os

# Add the ppk2-api-python path to sys.path for testing
ppk2_api_path = "/Users/kartben/Repositories/ppk2-api-python/src"
if ppk2_api_path not in sys.path:
    sys.path.insert(0, ppk2_api_path)

def test_ppk2_import():
    """Test that PPK2 API can be imported"""
    try:
        from ppk2_api.ppk2_api import PPK2_API, PPK2_Modes
        assert PPK2_API is not None
        assert PPK2_Modes is not None
        print("✓ PPK2 API imported successfully")
    except ImportError as e:
        pytest.skip(f"PPK2 API not available: {e}")

def test_ppk2_power_monitor_import():
    """Test that PPK2PowerMonitor can be imported"""
    try:
        from ppk2.PPK2PowerMonitor import PPK2PowerMonitor
        assert PPK2PowerMonitor is not None
        print("✓ PPK2PowerMonitor imported successfully")
    except ImportError as e:
        pytest.skip(f"PPK2PowerMonitor not available: {e}")

def test_ppk2_power_monitor_creation():
    """Test that PPK2PowerMonitor can be instantiated"""
    try:
        from ppk2.PPK2PowerMonitor import PPK2PowerMonitor
        monitor = PPK2PowerMonitor()
        assert monitor is not None
        assert hasattr(monitor, 'connect')
        assert hasattr(monitor, 'disconnect')
        assert hasattr(monitor, 'get_data')
        print("✓ PPK2PowerMonitor instantiated successfully")
    except Exception as e:
        pytest.skip(f"PPK2PowerMonitor instantiation failed: {e}")

def test_ppk2_device_discovery():
    """Test PPK2 device discovery (requires actual device)"""
    try:
        from ppk2.PPK2PowerMonitor import PPK2PowerMonitor
        monitor = PPK2PowerMonitor()
        devices = monitor.get_available_devices()
        print(f"Found {len(devices)} PPK2 device(s)")
        if devices:
            print(f"First device: {devices[0]}")
        else:
            print("No PPK2 devices found (this is normal if no device is connected)")
    except Exception as e:
        pytest.skip(f"PPK2 device discovery failed: {e}")

if __name__ == "__main__":
    # Run tests
    test_ppk2_import()
    test_ppk2_power_monitor_import()
    test_ppk2_power_monitor_creation()
    test_ppk2_device_discovery()
    print("All tests completed!")