.. _ipm_api:

Inter-Processor Mailbox (IPM)
#############################

Overview
********

The IPM (Inter-Processor Mailbox) API provides a lightweight signalling
mechanism for passing messages between processors or cores in a
multi-processor system. An IPM device exposes one or more independent
channels; each channel can carry a small in-band payload (up to
:c:func:`ipm_max_data_size_get` bytes) along with a message identifier.

Applications register a callback with :c:func:`ipm_register_callback` that is
invoked in interrupt context whenever a message arrives. The sender uses
:c:func:`ipm_send` to transmit a message, optionally blocking until the remote
side has consumed it.

.. note::
   For new designs, consider using the :ref:`mbox_api` which is the
   recommended successor to this API.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_IPM`

API Reference
*************

.. doxygengroup:: ipm_interface
