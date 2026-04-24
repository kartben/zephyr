.. _crypto_api:


Crypto APIs
###########

Overview
********

The crypto API provides a common interface for hardware or driver-backed cryptographic operations.
Applications interact with a crypto device, query its capabilities, create a session, and then run
cipher or hash operations through that session.

Crypto drivers differ in the algorithms, modes, key handling, buffering, and completion models they
support. Applications use :c:func:`crypto_query_hwcaps` to discover these capabilities and select a
compatible set of capability flags before starting a session.

Key concepts
************

The crypto API is organized around sessions and operation descriptors.

**Capabilities**
  Capability flags describe how a driver can be used. They indicate supported key handling,
  input/output buffer layout, synchronous or asynchronous completion, and mode-specific behavior
  such as IV handling. Drivers report supported flags with :c:func:`crypto_query_hwcaps`, and
  applications select the flags they want to use in the session context.

**Sessions**
  A session stores parameters that remain constant across operations. For cipher sessions this
  includes the selected algorithm, mode, operation type, key material, key length, and mode
  parameters. For hash sessions this includes the selected hash algorithm and capability flags.

**Operation packets**
  Operation packets describe the data for one operation. Cipher packets describe input and output
  buffers, while AEAD cipher packets also include associated data and an authentication tag. Hash
  packets describe the input data and digest output buffer.

**Completion model**
  Drivers can support synchronous operations, asynchronous operations, or both. In synchronous mode,
  the operation result is available when the API call returns. In asynchronous mode, the application
  registers a completion callback and the driver reports the result through that callback.

Typical application flow
========================

Typical use of the crypto API is:

#. Get the crypto device, usually from devicetree.
#. Call :c:func:`crypto_query_hwcaps` and choose capability flags supported by the driver.
#. Fill a :c:struct:`cipher_ctx` or :c:struct:`hash_ctx` with the application-selected session
   parameters.
#. Start the session with :c:func:`cipher_begin_session` or :c:func:`hash_begin_session`.
#. Fill a :c:struct:`cipher_pkt`, :c:struct:`cipher_aead_pkt`, or :c:struct:`hash_pkt` for each
   operation.
#. Run the operation using the API for the selected mode, such as :c:func:`cipher_cbc_op`,
   :c:func:`cipher_gcm_op`, :c:func:`hash_compute`, or :c:func:`hash_update`.
#. Free the session with :c:func:`cipher_free_session` or :c:func:`hash_free_session`.

Cipher operations
=================

Cipher sessions use :c:struct:`cipher_ctx`. The application selects the key representation,
capability flags, and mode-specific parameters before calling :c:func:`cipher_begin_session`.

The selected cipher mode determines which operation helper to call:

* :c:func:`cipher_block_op` for ECB single-block operations
* :c:func:`cipher_cbc_op` for CBC operations
* :c:func:`cipher_ctr_op` for CTR operations
* :c:func:`cipher_ccm_op` for CCM authenticated encryption
* :c:func:`cipher_gcm_op` for GCM authenticated encryption

Hash operations
===============

Hash sessions use :c:struct:`hash_ctx`. The application selects capability flags before calling
:c:func:`hash_begin_session`.

Applications can compute a digest in one step with :c:func:`hash_compute`, or provide input in
multiple chunks with :c:func:`hash_update` followed by :c:func:`hash_compute` to finalize the
digest.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_CRYPTO`

API Reference
*************

Generic crypto API
==================

.. doxygengroup:: crypto

Ciphers API
===========

.. doxygengroup:: crypto_cipher

Hash API
========

.. doxygengroup:: crypto_hash
