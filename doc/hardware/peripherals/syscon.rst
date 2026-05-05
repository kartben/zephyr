.. _syscon_api:

System Controller (SYSCON)
##########################

Overview
********

A system controller (SYSCON) is a block of memory-mapped registers used to
control miscellaneous system-level functions that do not belong to a specific
peripheral — for example, chip-identification registers, boot configuration,
clock muxes, and pin-function selects not covered by the pin controller.

The SYSCON driver API provides a minimal set of operations:

- :c:func:`syscon_get_base` — retrieve the base address of the SYSCON region.
- :c:func:`syscon_read_reg` / :c:func:`syscon_write_reg` — read or write a
  single 32-bit register at a given offset.
- :c:func:`syscon_get_size` — retrieve the size (in bytes) of the region.

SYSCON devices are typically used by other drivers (e.g., clock controllers,
pinctrl, or reset controllers) to access shared register banks, rather than
being accessed directly from application code.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_SYSCON`

API Reference
*************

.. doxygengroup:: syscon_interface
