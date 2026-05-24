.. zephyr:board:: pinecil_v2

Overview
********

The Pine64 Pinecil V2 is a portable USB-C soldering iron based on the Bouffalo
Lab BL706C00Q2I RISC-V SoC.

Hardware
********

The Pinecil V2 features:

- Bouffalo Lab BL706C00Q2I RISC-V core running at 144 MHz
- 88 KB usable SRAM
- 4 MB external flash
- 0.69 inch 96x16 monochrome I2C OLED display
- Two front-panel buttons
- USB-C power input and onboard USB PD controller
- BLE radio integrated in the BL706 SoC

Current Zephyr support covers the BL706-based console, the onboard OLED display,
the front-panel buttons, and the standard BL706 watchdog, entropy, flash, and
Bluetooth HCI blocks.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

To enter the Bouffalo Lab boot ROM, hold the **-** button while plugging the
Pinecil V2 into USB-C. Once the boot ROM enumerates, ``west flash`` can use the
``bflb_mcu_tool`` runner configured for this board.

Building
********

Build a simple application with:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: pinecil_v2/bl706c00q2i
   :goals: build

References
**********

.. target-notes::

.. _Pine64 Pinecil V2 wiki:
   https://wiki.pine64.org/wiki/Pinecil

.. _Pine64 Pinecil V2 schematic:
   https://files.pine64.org/doc/Pinecil/Pinecil_schematic_v2.0_20220608.pdf

.. _IronOS Pinecil V2 board support:
   https://github.com/Ralim/IronOS/tree/dev/source/Core/BSP/Pinecilv2
