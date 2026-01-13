.. zephyr:board:: stackchan

Overview
********

StackChan is a super kawaii AI desktop robot co-created by M5Stack and the user community. It uses
the M5Stack flagship IoT development kit :zephyr:board:`m5stack_cores3` as its main controller,
powered by an ESP32-S3 SoC featuring a 240 MHz dual-core processor, with 16MB Flash and 8MB PSRAM
onboard, supporting Wi-Fi and BLE.

The main unit integrates a 2.0-inch capacitive touch display, a 0.3 MP camera, a proximity sensor, a
9-axis IMU, a microSD card slot, a 1W speaker, dual microphones, and power/reset buttons.

The robot body includes:

- Two feedback servo motors (360° horizontal, 90° vertical)
- 12 RGB LEDs in two rows
- Three-zone capacitive touch panel on top
- Full-featured NFC module (ST25R3916)
- Infrared transmitter and receiver
- 700 mAh battery

Hardware
********

M5Stack StackChan features consist of:

- ESP32-S3 chip (dual-core Xtensa LX7 processor @240MHz, WIFI, OTG and CDC functions)
- PSRAM 8MB
- Flash 16MB
- LCD ISP 2", 320x240 pixel ILI9342C
- Capacitive multi touch FT6336U
- Speaker 1W AW88298
- Dual Microphones ES7210 Audio decoder
- RTC BM8563
- USB-C
- SD-Card slot
- PMIC AXP2101
- Battery 700mAh (robot body)
- Camera 30W pixel GC0308
- Geomagnetic sensor BMM150
- 6-Axis IMU BMI270
- Servo motors SCS0009 (x2, via UART)
- RGB LEDs (12x via PY32L020 IO expander)
- Touch panel Si12T
- NFC ST25R3916
- IR IRM56384

.. include:: ../../../espressif/common/soc-esp32s3-features.rst
   :start-after: espressif-soc-esp32s3-features

Supported Features
==================

.. zephyr:board-supported-hw::

System Requirements
*******************

.. include:: ../../../espressif/common/system-requirements.rst
   :start-after: espressif-system-requirements

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

.. include:: ../../../espressif/common/building-flashing.rst
   :start-after: espressif-building-flashing

.. include:: ../../../espressif/common/board-variants.rst
   :start-after: espressif-board-variants

Debugging
=========

.. include:: ../../../espressif/common/openocd-debugging.rst
   :start-after: espressif-openocd-debugging

References
**********

.. target-notes::

.. _`M5Stack StackChan Documentation`: https://docs.m5stack.com/en/StackChan
.. _`M5Stack CoreS3 Documentation`: https://docs.m5stack.com/en/core/CoreS3
