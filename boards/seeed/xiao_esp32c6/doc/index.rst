.. zephyr:board:: xiao_esp32c6

Overview
********

Seeed Studio XIAO ESP32C6 is powered by the highly-integrated ESP32-C6 SoC.
It consists of a high-performance (HP) 32-bit RISC-V processor, which can be clocked up to 160 MHz,
and a low-power (LP) 32-bit RISC-V processor, which can be clocked up to 20 MHz.
It has a 320KB ROM, a 512KB SRAM, and works with external flash.
This board integrates complete Wi-Fi, Bluetooth LE, Zigbee, and Thread functions.
For more information, check `Seeed Studio XIAO ESP32C6`_ .

The :ref:`mr60bha2 kit variant <xiao_esp32c6_mr60bha2>` combines the XIAO ESP32C6
with the Seeed MR60BHA2 60 GHz mmWave radar, a BH1750 ambient light sensor,
and an onboard WS2812 RGB LED. For hardware details, see the `Seeed Studio MR60BHA2 kit`_.

Hardware
********

This board is based on the ESP32-C6 with 4MB of flash, integrating 2.4 GHz Wi-Fi 6,
Bluetooth 5.3 (LE) and the 802.15.4 protocol. It has an USB-C port for programming
and debugging, integrated battery charging and an U.FL external antenna connector.
It is based on a standard XIAO 14 pin pinout.

.. include:: ../../../espressif/common/soc-esp32c6-features.rst
   :start-after: espressif-soc-esp32c6-features

Supported Features
==================

.. zephyr:board-supported-hw::

Connections and IOs
===================

The board uses a standard XIAO pinout, the default pin mapping is the following:

.. figure:: img/xiao_esp32c6_pinout.webp
   :align: center
   :alt: XIAO ESP32C6 Pinout

   XIAO ESP32C6 Pinout

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

Board Variants
**************

.. _xiao_esp32c6_mr60bha2:

XIAO ESP32C6 MR60BHA2 kit
=========================

This variant models the pre-assembled MR60BHA2 breathing and heartbeat kit.
In addition to the base XIAO ESP32C6 features, it enables:

* the MR60BHA2 UART radar on ``xiao_serial``
* the BH1750 ambient light sensor on ``i2c0``
* the onboard WS2812 RGB LED on the I2S peripheral

To build for the kit variant:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: xiao_esp32c6/esp32c6/hpcore/mr60bha2
   :goals: build

References
**********

.. target-notes::

.. _`Seeed Studio XIAO ESP32C6`: https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/
.. _`Seeed Studio MR60BHA2 kit`: https://wiki.seeedstudio.com/getting_started_with_mr60bha2_mmwave_kit/
