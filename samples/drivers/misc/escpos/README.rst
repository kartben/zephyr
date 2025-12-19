.. zephyr:code-sample:: escpos
   :name: ESC/POS Thermal Printer
   :relevant-api: escpos

   Print text and demonstrate text formatting on an ESC/POS thermal printer.

Overview
********

This sample demonstrates how to use the ESC/POS thermal printer driver to print
text with various formatting options such as bold and underline.

Requirements
************

To use this sample, the following hardware is required:

* A board with UART support
* An ESC/POS compatible thermal printer

Wiring
******

Connect the printer to a UART port on the board. The printer should be
configured to use the same baud rate as the UART port.

Building and Running
********************

This sample can be run on any board that has UART enabled. A device tree
overlay is required to define the printer device node.

Example overlay:

.. code-block:: devicetree

   &uart1 {
       status = "okay";

       escpos: escpos {
           compatible = "epson,escpos";
       };
   };

Then build and flash:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/misc/escpos
   :board: <your_board>
   :goals: flash
   :compact:

Sample Output
*************

The sample will print several lines of text demonstrating different text
formatting options:

* Normal text
* Bold text
* Underlined text

After printing, the paper is automatically cut (if the printer has a cutter).
