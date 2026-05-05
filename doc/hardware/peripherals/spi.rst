.. _spi_api:

Serial Peripheral Interface (SPI) Bus
#####################################

Overview
********

The SPI (Serial Peripheral Interface) driver API provides a synchronous,
full-duplex serial communication bus interface. It supports both controller
(master) and peripheral (target/slave) roles and allows transactions to be
composed of multiple contiguous buffers described as scatter-gather lists.

Key features of the API:

- Configurable clock polarity and phase (CPOL/CPHA modes 0–3).
- Configurable word size, bit order, and bus frequency.
- Optional chip-select management via GPIO or hardware CS lines.
- Lock/release mechanism for bus ownership in multi-threaded environments.
- Asynchronous (non-blocking) operation via callback or :ref:`rtio`.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_SPI`

API Reference
*************

.. doxygengroup:: spi_interface
