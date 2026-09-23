.. zephyr:board:: m5stack_stackchan

Overview
********

M5Stack StackChan is a desktop robot from M5Stack based on the open-source Stack-chan
project by Shishikawa. It consists of the *StackChan Core* main unit (an ESP32-S3
controller that shares its design with the M5Stack CoreS3) and a robot body that
holds the battery, two serial servos, LEDs, a touch panel, IR and NFC.

(Image courtesy of M5Stack, taken from the `M5Stack StackChan product page`_.)

Hardware
********

StackChan Core (main unit):

- ESP32-S3 chip (dual-core Xtensa LX7 processor @240MHz, WIFI, OTG and CDC functions)
- PSRAM 8MB (Quad)
- Flash 16MB
- LCD ISP 2", 320x240 pixel ILI9342C
- Capacitive multi touch FT6336U
- Speaker 1W AW88298
- Dual Microphones ES7210 Audio decoder
- RTC BM8563
- PMIC AXP2101
- GPIO expander AW9523B
- Camera 30W pixel GC0308
- Geomagnetic sensor BMM150
- Proximity sensor LTR-553ALS-WA
- 6-Axis IMU BMI270
- microSD card slot
- USB-C

Robot body:

- Battery 550mAh 3.7 V with INA226 battery monitor
- Two SCS0009 serial servos (yaw / pitch) on UART1 (GPIO6 TX / GPIO7 RX, 1 Mbps half-duplex)
- IO expander PY32L020 (servo power enable, 12 RGB LEDs)
- Touch panel Si12T (3 zones)
- IR transmitter (GPIO5) and IRM56384 IR receiver (GPIO10)
- NFC ST25R3916
- Three Grove (HY2.0-4P) ports: PORT.A (I2C), PORT.B (GPIO / ADC), PORT.C (UART)

The M-Bus of the main unit is occupied by the robot body and is not exposed.

Grove ports
===========

.. list-table::
   :header-rows: 1

   * - Port
     - Pins
     - Zephyr node
   * - PORT.A
     - GPIO1 (SCL), GPIO2 (SDA)
     - ``i2c1`` (``zephyr_i2c`` / ``grove_header``)
   * - PORT.B
     - GPIO8, GPIO9
     - ``gpio0`` (also usable as ADC inputs)
   * - PORT.C
     - GPIO17 (TX), GPIO18 (RX)
     - ``uart2`` (``grove_uart``), disabled by default

UART1 (GPIO6 / GPIO7) is wired to the servos inside the body and is not
available on any port. The 5 V supply of the Grove ports is controlled by the
``bus_5v`` regulator.

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

The board has no GPIO-connected LED, so :zephyr:code-sample:`blinky` does not
apply. The ``led0`` alias points to the AXP2101 status LED, which is driven
through the LED API; it can be exercised from the shell with
``CONFIG_LED_SHELL=y`` (``led on led 0``).

Debugging
=========

.. include:: ../../../espressif/common/openocd-debugging.rst
   :start-after: espressif-openocd-debugging

References
**********

.. target-notes::

.. _`M5Stack StackChan Documentation`: https://docs.m5stack.com/en/StackChan
.. _`M5Stack StackChan product page`: https://shop.m5stack.com/products/stackchan-kawaii-co-created-open-source-ai-desktop-robot
.. _`StackChan Core Documentation`: https://docs.m5stack.com/en/core/StackChan_Core
