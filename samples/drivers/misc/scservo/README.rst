.. zephyr:code-sample:: scservo
   :name: SCServo Serial Bus Servo
   :relevant-api: scservo

   Control FeeeTech SCServo serial bus servos (SCSCL/STS series).

Overview
********

This sample demonstrates how to use the SCServo driver to control FeeeTech
serial bus servos. These smart servos communicate over a half-duplex UART
bus and support position control, velocity control, and feedback (position,
temperature, voltage, load).

The sample shows how to:

- Ping servos to verify communication
- Read servo feedback (position, voltage, temperature)
- Enable torque and move servos to target positions
- Perform a simple sweep demonstration

Requirements
************

This sample requires a board with SCServo devices configured in devicetree.
The servos must be connected to a UART port configured for half-duplex
communication.

The following devicetree aliases must be defined:

- ``servo0``: Primary servo device
- ``servo1``: (Optional) Secondary servo device

Wiring
******

SCServo devices use a half-duplex serial bus. Connect the servo's DATA line
to both the TX and RX pins of the UART through appropriate circuitry (typically
a tri-state buffer or resistor network for TX/RX isolation).

For M5Stack StackChan, the servos are pre-wired to UART1 on GPIO6 (TX) and
GPIO7 (RX) with on-board half-duplex circuitry.

Building and Running
********************

For M5Stack StackChan:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/misc/scservo
   :board: stackchan/esp32s3/procpu
   :goals: build flash
   :compact:

Sample Output
*************

.. code-block:: console

   *** Booting Zephyr OS build v4.0.0 ***
   [00:00:00.100,000] <inf> scservo_sample: SCServo sample started
   [00:00:00.100,000] <inf> scservo_sample: Servo0 device is ready
   [00:00:00.100,000] <inf> scservo_sample: Servo1 device is ready
   [00:00:00.110,000] <inf> scservo_sample: Servo0 ping successful
   [00:00:00.120,000] <inf> scservo_sample: Servo1 ping successful
   [00:00:00.130,000] <inf> scservo_sample: Servo0 position: 512
   [00:00:00.140,000] <inf> scservo_sample: Servo0 voltage: 7.4 V
   [00:00:00.150,000] <inf> scservo_sample: Servo0 temperature: 35 C
   [00:00:00.160,000] <inf> scservo_sample: Moving servo0 to position 512 (center)...
   [00:00:01.660,000] <inf> scservo_sample: Servo0 position after move: 512
   [00:00:01.660,000] <inf> scservo_sample: Starting position sweep demo...
   ...
   [00:00:06.000,000] <inf> scservo_sample: SCServo sample complete

References
**********

- `FeeeTech SCS Series Servo Datasheet`_

.. _FeeeTech SCS Series Servo Datasheet: https://www.feetechrc.com/
