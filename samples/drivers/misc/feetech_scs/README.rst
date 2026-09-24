.. zephyr:code-sample:: feetech-scs
   :name: Feetech SCS servo
   :relevant-api: feetech_scs_interface

   Read feedback from and move Feetech SCS serial bus servos.

Overview
********

For each ``feetech,scs0009`` servo in the devicetree, this sample:

#. checks that the servo answers and prints its position, load, supply voltage and temperature,
#. moves it by about 15 degrees and back to where it started,
#. disables its torque.

It then prints the servo positions twice per second while you move them by hand.

Requirements
************

A board with Feetech SCS servos described in its devicetree, such as
:zephyr:board:`m5stack_stackchan`.

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/misc/feetech_scs
   :board: m5stack_stackchan/esp32s3/procpu
   :goals: build flash
   :compact:
