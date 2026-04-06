.. _otp:

One Time Programmable (OTP) memory devices
##########################################

Overview
********

OTP memory devices store data that is intended to be permanent.  Each
memory cell can be written **exactly once**; once a bit is programmed it
cannot be erased or changed.  This irreversibility makes OTP memory
suitable for storing data that must be trustworthy for the entire lifetime
of the device.

Typical use cases include:

- **Device identity** – unique serial numbers, board revision codes, or
  factory-assigned identifiers that remain constant across firmware
  updates and power cycles.

- **Cryptographic material** – root keys, key derivation seeds, or
  certificate thumbprints used during secure boot or attestation flows.
  Data stored in OTP cannot be overwritten by a compromised firmware
  image.

- **Lifecycle flags** – monotonic counters, provisioning status, or
  debug-lock bits that must advance forward and can never be rolled back.

- **Calibration constants** – trim values, sensor offsets, or
  factory-calibrated coefficients written during manufacturing test.

.. warning::

   Because programming is irreversible, always validate the data you
   intend to write and test your application against the OTP emulator
   (:dtcompatible:`zephyr,otp-emul`) before deploying to hardware.
   Zephyr gates write access behind :kconfig:option:`CONFIG_OTP_PROGRAM`
   so that read-only builds cannot accidentally program cells.

Architecture
************

The OTP API is deliberately minimal:

- :c:func:`otp_read` – reads a byte range from the OTP region at a given
  offset.  Always available.

- :c:func:`otp_program` – writes a byte range to the OTP region.
  Only available when :kconfig:option:`CONFIG_OTP_PROGRAM` is enabled.

Both functions are thin wrappers around driver callbacks in
:c:struct:`otp_driver_api`.  Drivers are responsible for enforcing
hardware-level write-once semantics (e.g., masking out previously set
bits or returning an error if a cell has already been written to a
different value).

Shell support
*************

Enable :kconfig:option:`CONFIG_OTP_SHELL` to get interactive ``otp read``
and ``otp program`` commands via the :ref:`shell_api`.  The shell provides
a ``-y`` flag to skip the safety confirmation prompt when programming from
automated scripts.

Samples
*******

- :zephyr:code-sample:`otp` – programs a simple provisioning record
  (serial number, hardware revision) and reads it back.

.. toctree::
   :maxdepth: 2

   api.rst
