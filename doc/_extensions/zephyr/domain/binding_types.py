"""
Zephyr Binding Types
###################

SPDX-FileCopyrightText: Copyright (c) 2025 The Linux Foundation
SPDX-License-Identifier: Apache-2.0

This module contains the mapping of binding types to their human-readable names and descriptions.
Each entry can be either:
- A string representing the human-readable name
- A tuple of (abbreviation, full name) for cases where an abbreviation exists
"""

BINDING_TYPES = {
    "acpi": ("ACPI", "Advanced Configuration and Power Interface"),
    "adc": ("ADC", "Analog to Digital Converter"),
    "alh": ("ALH", "Audio Link Hub"),
    "arc": "ARC architecture",
    "arm": "ARM architecture",
    "audio": "Audio",
    "auxdisplay": "Auxiliary Display",
    "battery": "Battery",
    "base": "Base",
    "bluetooth": "Bluetooth",
    "cache": "Cache",
    "can": ("CAN", "Controller Area Network"),
    "charger": "Charger",
    "clock": "Clock control",
    "coredump": "Core dump",
    "counter": "Counter",
    "cpu": "CPU",
    "crypto": "Cryptographic accelerator",
    "dac": ("DAC", "Digital to Analog Converter"),
    "dai": ("DAI", "Digital Audio Interface"),
    "debug": "Debug",
    "dfpmcch": "DFPMCCH",
    "dfpmccu": "DFPMCCU",
    "disk": "Disk",
    "display": "Display",
    "display/panel": "Display panel",
    "dma": ("DMA", "Direct Memory Access"),
    "dsa": ("DSA", "Distributed Switch Architecture"),
    "edac": ("EDAC", "Error Detection and Correction"),
    "espi": ("eSPI", "Enhanced Serial Peripheral Interface"),
    "ethernet": "Ethernet",
    "firmware": "Firmware",
    "flash_controller": "Flash controller",
    "fpga": ("FPGA", "Field Programmable Gate Array"),
    "fs": "File system",
    "fuel-gauge": "Fuel gauge",
    "gnss": ("GNSS", "Global Navigation Satellite System"),
    "gpio": ("GPIO", "General Purpose Input/Output"),
    "haptics": "Haptics",
    "hda": ("HDA", "High Definition Audio"),
    "hdlc_rcp_if": "IEEE 802.15.4 HDLC RCP interface",
    "hwinfo": "Hardware information",
    "hwspinlock": "Hardware spinlock",
    "i2c": ("I2C", "Inter-Integrated Circuit"),
    "i2s": ("I2S", "Inter-IC Sound"),
    "i3c": ("I3C", "Improved Inter-Integrated Circuit"),
    "ieee802154": "IEEE 802.15.4",
    "iio": ("IIO", "Industrial I/O"),
    "input": "Input",
    "interrupt-controller": "Interrupt controller",
    "ipc": ("IPC", "Inter-Processor Communication"),
    "ipm": ("IPM", "Inter-Processor Mailbox"),
    "kscan": "Keyscan",
    "led": ("LED", "Light Emitting Diode"),
    "led_strip": ("LED", "Light Emitting Diode"),
    "lora": "LoRa",
    "mbox": "Mailbox",
    "mdio": ("MDIO", "Management Data Input/Output"),
    "memory-controllers": "Memory controller",
    "memory-window": "Memory window",
    "mfd": ("MFD", "Multi-Function Device"),
    "mhu": ("MHU", "Mailbox Handling Unit"),
    "net": "Networking",
    "mipi-dbi": ("MIPI DBI", "Mobile Industry Processor Interface Display Bus Interface"),
    "mipi-dsi": ("MIPI DSI", "Mobile Industry Processor Interface Display Serial Interface"),
    "misc": "Miscellaneous",
    "mm": "Memory management",
    "mmc": ("MMC", "MultiMediaCard"),
    "mmu_mpu": ("MMU / MPU", "Memory Management Unit / Memory Protection Unit"),
    "modem": "Modem",
    "mspi": "Multi-bit SPI",
    "mtd": ("MTD", "Memory Technology Device"),
    "wireless": "Wireless network",
    "options": "Options",
    "ospi": "Octal SPI",
    "pcie": ("PCIe", "Peripheral Component Interconnect Express"),
    "peci": ("PECI", "Platform Environment Control Interface"),
    "phy": "PHY",
    "pinctrl": "Pin control",
    "pm_cpu_ops": "Power management CPU operations",
    "power": "Power management",
    "power-domain": "Power domain",
    "ppc": "PPC architecture",
    "ps2": ("PS/2", "Personal System/2"),
    "pwm": ("PWM", "Pulse Width Modulation"),
    "qspi": "Quad SPI",
    "regulator": "Regulator",
    "reserved-memory": "Reserved memory",
    "reset": "Reset controller",
    "retained_mem": "Retained memory",
    "retention": "Retention",
    "riscv": "RISC-V architecture",
    "rng": ("RNG", "Random Number Generator"),
    "rtc": ("RTC", "Real Time Clock"),
    "sd": "SD",
    "sdhc": "SDHC",
    "sensor": "Sensors",
    "serial": "Serial controller",
    "shi": ("SHI", "Secure Hardware Interface"),
    "sip_svc": ("SIP", "Service in Platform"),
    "smbus": ("SMBus", "System Management Bus"),
    "sound": "Sound",
    "spi": ("SPI", "Serial Peripheral Interface"),
    "sram": "SRAM",
    "stepper": "Stepper",
    "syscon": "System controller",
    "tach": "Tachometer",
    "tcpc": ("TCPC", "USB Type-C Port Controller"),
    "test": "Test",
    "timer": "Timer",
    "timestamp": "Timestamp",
    "usb": "USB",
    "usb-c": "USB Type-C",
    "uac2": "USB Audio Class 2",
    "video": "Video",
    "virtualization": "Virtualization",
    "w1": "1-Wire",
    "watchdog": "Watchdog",
    "wifi": "Wi-Fi",
    "xen": "Xen",
    "xspi": ("XSPI", "Expanded Serial Peripheral Interface"),
}
