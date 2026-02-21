.. _pcie_api:

Peripheral Component Interconnect express Bus (PCIe)
####################################################

Overview
********

PCI Express (PCIe) is a high-speed serial computer expansion bus standard
used to connect hardware devices to a processor. Zephyr provides a PCIe host
API that allows drivers to discover, configure, and access PCIe devices via
Base Address Registers (BARs), interrupt routing, and capability structures.

The API supports both MSI and MSI-X interrupt delivery as well as extended
capabilities and virtual channels on platforms where the PCIe host controller
exposes them.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_PCIE`

API Reference
*************

.. doxygengroup:: pcie_host_interface
