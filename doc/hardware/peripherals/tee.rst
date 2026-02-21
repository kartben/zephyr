.. _tee_api:

Trusted Execution Environment (TEE)
####################################

Overview
********

The TEE (Trusted Execution Environment) driver API provides a generic
interface for interacting with a secure co-processor or a Trusted OS running
in the platform's secure world. Common implementations include Arm TrustZone
(OP-TEE) and dedicated secure micro-controllers.

The API allows the normal-world (rich OS) side to:

- Query the TEE version and its capabilities with :c:func:`tee_get_version`.
- Open and close sessions to Trusted Applications (TAs) with
  :c:func:`tee_open_session` and :c:func:`tee_close_session`.
- Invoke commands within an open session with :c:func:`tee_invoke_func`.
- Register, allocate, and free shared memory buffers that can be accessed by
  both worlds using :c:func:`tee_shm_register`, :c:func:`tee_shm_alloc`, and
  :c:func:`tee_shm_free`.
- Cancel a pending TEE operation with :c:func:`tee_cancel`.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_TEE`

API Reference
*************

.. doxygengroup:: tee_interface
