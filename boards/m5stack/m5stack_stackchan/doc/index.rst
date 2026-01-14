.. zephyr:board:: m5stack_stackchan

Overview
********

M5Stack StackChan is a JavaScript-driven M5Stack-embedded super-kawaii robot based on the ESP32-S3.
It features an expressive display, servo motors for pan/tilt head movement, and various sensors.
StackChan is designed as an open-source robotics platform that runs on M5Stack CoreS3 hardware.

The StackChan board is built on the M5Stack CoreS3 platform and includes:

- ESP32-S3 chip (dual-core Xtensa LX7 processor @240MHz)
- PSRAM 8MB
- Flash 16MB
- LCD IPS 2", 320x240 pixel ILI9342C
- Capacitive multi-touch FT6336U
- Speaker 1W AW88298
- Dual Microphones ES7210 Audio decoder
- RTC BM8563
- USB-C
- SD-Card slot
- PMIC AXP2101
- Battery 500mAh 3.7V
- Camera 30W pixel GC0308
- Geomagnetic sensor BMM150
- Proximity sensor LTR-553ALS-WA
- 6-Axis IMU BMI270
- PWM servo control (2 channels) for pan/tilt movement

Hardware
********

The StackChan board features:

ESP32-S3
========

- Xtensa dual-core 32-bit LX7 microprocessor
- Up to 240 MHz clock frequency
- 512 KB of SRAM
- 384 KB of ROM
- 16 MB of Flash
- 8 MB of PSRAM
- IEEE 802.11 b/g/n Wi-Fi connectivity
- Bluetooth 5 (LE)
- 45 × programmable GPIOs

Display and Touch
=================

- 2.0-inch IPS LCD display (320x240 pixels)
- ILI9342C display controller
- FT6336U capacitive touch controller
- Supports multi-touch input

Power Management
================

- AXP2101 Power Management IC
- Built-in 500mAh lithium battery
- USB-C charging and programming
- Multiple power rails for peripherals

Servo Control
=============

- 2 PWM channels for servo motor control
- Dedicated power supply for servos
- Support for standard hobby servos (SG90) or serial servos (RS304MD)
- Pan and tilt movement for expressive robot head

Sensors
=======

- BMI270: 6-axis IMU (3-axis accelerometer + 3-axis gyroscope)
- BMM150: 3-axis magnetometer
- LTR-553ALS-WA: Proximity and ambient light sensor
- BM8563: Real-time clock

Audio
=====

- AW88298 1W speaker amplifier
- ES7210 audio decoder with dual microphones
- Support for voice input and audio output

Connectivity
============

- USB-C for programming and power
- SD card slot for storage expansion
- Grove-compatible I2C ports (PortB and PortC)
- UART, SPI, I2C interfaces

.. include:: ../../../espressif/common/soc-esp32s3-features.rst
   :start-after: espressif-soc-esp32s3-features

Supported Features
==================

The M5Stack StackChan board configuration supports the following hardware features:

.. zephyr:board-supported-hw::

System Requirements
*******************

Prerequisites
=============

.. include:: ../../../espressif/common/system-requirements.rst
   :start-after: espressif-system-requirements

Programming and Debugging
*************************

Building
========

.. zephyr:board-supported-runners::

.. include:: ../../../espressif/common/building-flashing.rst
   :start-after: espressif-building-flashing

Servo Control
=============

The StackChan board includes two PWM channels (GPIO13 and GPIO14) for controlling servo motors.
These are typically used for pan (horizontal) and tilt (vertical) head movement:

- Servo Pan: GPIO13 (LEDC Channel 0)
- Servo Tilt: GPIO14 (LEDC Channel 1)

The servos can be powered from the onboard battery or external power supply.

Debugging
=========

.. include:: ../../../espressif/common/openocd-debugging.rst
   :start-after: espressif-openocd-debugging

References
**********

.. target-notes::

.. _M5Stack StackChan Documentation: https://docs.m5stack.com/en/app/StackChan
.. _StackChan GitHub Repository: https://github.com/stack-chan/stack-chan
.. _M5Stack CoreS3 Documentation: http://docs.m5stack.com/en/core/CoreS3
.. _M5Stack CoreS3 Schematic: https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/K128%20CoreS3/Sch_M5_CoreS3_v1.0.pdf
.. _ESP32-S3 Datasheet: https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
.. _ESP32-S3 Technical Reference Manual: https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf
