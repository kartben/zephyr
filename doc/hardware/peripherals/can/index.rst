.. _can:

Controller Area Network (CAN)
#############################

Overview
********

Controller Area Network (CAN) is a robust serial communication bus designed for reliable
communication in automotive and industrial environments. Originally developed for automotive
applications, CAN provides multi-master communication with priority-based arbitration, error
detection, and automatic retransmission of corrupted messages.

The CAN subsystem in Zephyr provides comprehensive support for CAN communication, including:

**CAN Controller**
  Interface for CAN controller hardware peripherals.
  Support for Classical CAN (CAN 2.0) and CAN FD (Flexible Data-Rate).
  Message transmission and reception with filtering.
  Error handling and bus-off recovery.
  Timing configuration and bit-rate settings.

**CAN Transceiver**
  Interface for CAN transceiver hardware.
  Control of transceiver operating modes (normal, standby, silent).
  Maximum data rate configuration.

**CAN Shell**
  Command-line interface for CAN debugging and testing.
  Send and receive CAN frames interactively.
  Configure bit-timing and filters from the shell.

Key Features
************

* Support for standard (11-bit) and extended (29-bit) CAN identifiers
* Configurable hardware filters for message reception
* CAN FD support for higher data rates and larger payloads
* Bus timing calculation and configuration
* Error state tracking and bus-off recovery
* Loopback mode for testing

Available CAN Documentation
****************************

.. toctree::
   :maxdepth: 2

   controller.rst
   transceiver.rst
   shell.rst
