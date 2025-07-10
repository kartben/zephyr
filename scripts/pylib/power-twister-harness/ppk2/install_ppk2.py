#!/usr/bin/env python3
# Copyright: (c)  2025, Intel Corporation
# PPK2 Installation Script

import subprocess
import sys
import os

def install_package(package):
    """Install a Python package using pip"""
    try:
        subprocess.check_call([sys.executable, "-m", "pip", "install", package])
        print(f"✓ Successfully installed {package}")
        return True
    except subprocess.CalledProcessError as e:
        print(f"✗ Failed to install {package}: {e}")
        return False

def check_package(package):
    """Check if a package is installed"""
    try:
        __import__(package)
        print(f"✓ {package} is already installed")
        return True
    except ImportError:
        print(f"✗ {package} is not installed")
        return False

def main():
    print("PPK2 Power Monitor Installation Script")
    print("=" * 40)

    # Check Python version
    if sys.version_info < (3, 7):
        print("✗ Python 3.7 or higher is required")
        sys.exit(1)
    else:
        print(f"✓ Python {sys.version_info.major}.{sys.version_info.minor} detected")

    # Required packages
    packages = [
        ("ppk2-api-python", "ppk2_api"),
        ("pyserial", "serial")
    ]

    print("\nChecking and installing dependencies...")

    all_installed = True
    for package_name, import_name in packages:
        if not check_package(import_name):
            if not install_package(package_name):
                all_installed = False

    if all_installed:
        print("\n✓ All dependencies installed successfully!")
        print("\nTo test the installation, run:")
        print("  python test_ppk2_integration.py")
        print("\nTo see an example, run:")
        print("  python ppk2/example_ppk2.py")
    else:
        print("\n✗ Some dependencies failed to install")
        print("Please install them manually:")
        for package_name, _ in packages:
            print(f"  pip install {package_name}")
        sys.exit(1)

if __name__ == "__main__":
    main()