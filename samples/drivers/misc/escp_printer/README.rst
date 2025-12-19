.. zephyr:code-sample:: escp-printer
   :name: Epson ESC/P Printer
   :relevant-api: escp_printer

   Print text and demonstrate text formatting on an Epson ESC/P printer.

Overview
********

This sample demonstrates how to use the Epson ESC/P printer driver to print
text with various formatting options such as bold, italic, and underline.

Requirements
************

To use this sample, the following hardware is required:

* A board with UART support
* An Epson ESC/P compatible printer

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

       escp: escp {
           compatible = "epson,escp";
       };
   };

Then build and flash:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/misc/escp_printer
   :board: <your_board>
   :goals: flash
   :compact:

Sample Output
*************

The sample will print several lines of text demonstrating different text
formatting options:

* Normal text
* Bold text
* Italic text
* Underlined text
