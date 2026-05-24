.. zephyr:board:: pinecil_v2

Overview
********

The Pinecil V2 is a smart soldering iron from Pine64 based on the Bouffalo Lab
BL706C00Q2I RISC-V SoC.

Hardware
********

The board provides:

- Bouffalo Lab BL706C00Q2I MCU
- 4 MB external flash
- USB-C connector
- Two front-panel buttons
- Internal die temperature sensor (TSEN via ADC)
- I2C and SPI interfaces used by onboard peripherals

Supported Features
==================

.. zephyr:board-supported-hw::

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

Building
========

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: pinecil_v2
   :goals: build

Flashing
========

Use ``bflb_mcu_tool`` through ``west flash``.

References
**********

.. target-notes::

.. _`Pinecil V2 Schematic`: https://files.pine64.org/doc/Pinecil/Pinecil_schematic_v2.0_20220608.pdf
.. _`Pinecil Wiki`: https://wiki.pine64.org/wiki/Pinecil
