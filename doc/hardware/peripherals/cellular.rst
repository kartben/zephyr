.. _cellular_api:

Cellular Modem
##############

Overview
********

The cellular modem driver API provides a common interface for querying the
capabilities and network state of cellular modem devices (2G/3G/4G/5G). It is
intended to complement higher-level modem management frameworks (such as
:ref:`modem`) by exposing hardware-level information about the radio interface.

The API allows applications and the network stack to:

- Retrieve the set of access technologies supported by the modem with
  :c:func:`cellular_get_modem_info`.
- Query and configure the list of active network bands with
  :c:func:`cellular_get_network` and :c:func:`cellular_set_network`.

The cellular API is transport-agnostic and does not cover AT command
processing, data connections, or SIM management; those concerns are handled
at a higher layer (e.g., the modem subsystem or an application-level library).

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_MODEM_CELLULAR`

API Reference
*************

.. doxygengroup:: cellular_interface
