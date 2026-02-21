.. _fpga_api:

Field-Programmable Gate Array (FPGA)
#####################################

Overview
********

The FPGA driver API provides a common interface for loading bitstreams into
field-programmable gate arrays that are connected to (or embedded in) the
host SoC. It is typically used in systems where Zephyr acts as the host
processor responsible for managing the FPGA lifecycle.

The API exposes the following operations:

- :c:func:`fpga_get_status` — query whether the FPGA is active (ready to
  receive a bitstream) or inactive.
- :c:func:`fpga_load` — program the FPGA by transferring a bitstream image.
- :c:func:`fpga_reset` — reset the FPGA to its unprogrammed state.
- :c:func:`fpga_on` / :c:func:`fpga_off` — power the FPGA on or off.
- :c:func:`fpga_get_info` — retrieve a driver-defined information string.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_FPGA`

API Reference
*************

.. doxygengroup:: fpga_interface
