.. _crc_api:

Cyclic Redundancy Check (CRC)
#############################

Overview
********

A Cyclic Redundancy Check (CRC) is a short checksum obtained by dividing a block of data by a
generator polynomial, used to detect accidental corruption of data in storage and communication
systems. Many SoCs contain a CRC calculation unit that performs this division in hardware, so the
CPU only has to program the algorithm parameters, feed the input bytes and read the result back.

The CRC driver API provides a generic interface to such hardware units. The application describes
the algorithm in a context structure, passes the context to the driver together with one or more
input buffers, and reads the checksum back from the same context.

This API is distinct from the :ref:`CRC subsystem <crc>`, which provides portable software
implementations of the same algorithms behind functions such as :c:func:`crc32_ieee` and
:c:func:`crc16_ccitt`. The subsystem can be configured to run those functions on a hardware unit
through this driver API, see :ref:`crc_driver_configuration` below. Key concepts include:

**CRC device**
  A CRC unit is an ordinary :c:struct:`device` described by a devicetree node. The ``zephyr,crc``
  chosen node designates the unit that the CRC subsystem uses as an accelerator; the in-tree sample
  and test obtain the same device with ``DEVICE_DT_GET(DT_CHOSEN(zephyr_crc))``.

**CRC context**
  A :c:struct:`crc_ctx` structure describes one computation: the algorithm
  (:c:member:`crc_ctx.type`), the generator polynomial (:c:member:`crc_ctx.polynomial`), the
  initial value (:c:member:`crc_ctx.seed`) and the reflection flags (:c:member:`crc_ctx.reversed`).
  The driver tracks the progress of the computation in :c:member:`crc_ctx.state` and stores the
  checksum in :c:member:`crc_ctx.result`.

**Computation steps**
  A computation is the sequence :c:func:`crc_begin`, one or more :c:func:`crc_update` calls,
  :c:func:`crc_finish`. The first call programs the unit from the context and reserves it, the
  update calls feed data, and the last call reads the final value and releases the unit.
  :c:func:`crc_verify` then compares :c:member:`crc_ctx.result` with an expected value.

Describing a computation
************************

The application fills the following :c:struct:`crc_ctx` fields before calling
:c:func:`crc_begin`. The remaining fields must start zeroed, which leaves the context in
:c:enumerator:`CRC_STATE_IDLE`.

**Algorithm type** (:c:member:`crc_ctx.type`)
  One of the :c:enum:`crc_type` enumerators shared with the CRC subsystem, such as
  :c:enumerator:`CRC8`, :c:enumerator:`CRC16_CCITT` or :c:enumerator:`CRC32_IEEE`. Each driver
  supports a subset of the enumerators and rejects the others with ``-ENOTSUP``.

**Polynomial** (:c:member:`crc_ctx.polynomial`)
  The generator polynomial in normal, most significant bit first form, for example
  :c:macro:`CRC16_CCITT_POLY` (``0x1021``) or :c:macro:`CRC32_IEEE_POLY` (``0x04C11DB7``), as
  listed in :zephyr_file:`include/zephyr/sys/crc.h`. Drivers check that the polynomial matches
  what the unit can compute for the selected type and reject other values with ``-EINVAL``.

**Seed** (:c:member:`crc_ctx.seed`)
  The initial value loaded into the CRC register before the first byte is processed.
  :zephyr_file:`include/zephyr/drivers/crc.h` defines the conventional seed of each type, for
  example ``CRC16_CCITT_INIT_VAL`` (``0``) and ``CRC32_IEEE_INIT_VAL`` (``0xFFFFFFFF``).

**Reflection flags** (:c:member:`crc_ctx.reversed`)
  A bitmask of :c:macro:`CRC_FLAG_REVERSE_INPUT`, which bit-reverses every input byte before it
  enters the divider, and :c:macro:`CRC_FLAG_REVERSE_OUTPUT`, which bit-reverses the final
  register value. Together they select the reflected variant of an algorithm; the polynomial stays
  in normal form either way. The driver test, for instance, computes :c:enumerator:`CRC16` with
  :c:macro:`CRC16_POLY` and both flags set, and :c:enumerator:`CRC16_ITU_T` with
  :c:macro:`CRC16_CCITT_POLY` and no flags.

Whether the driver applies the final inversion that some algorithms specify depends on the type.
For :c:enumerator:`CRC32_IEEE` the drivers invert the register value before storing it, so
:c:member:`crc_ctx.result` equals the value returned by the software :c:func:`crc32_ieee`. For
:c:enumerator:`CRC32_C` the register value is stored as is and the caller applies the final
``0xFFFFFFFF`` XOR, as the subsystem does in its hardware backed :c:func:`crc32_c`.

Which types a driver supports is advertised at build time by hidden Kconfig symbols named
``CRC_DRIVER_HAS_<TYPE>``, such as :kconfig:option:`CONFIG_CRC_DRIVER_HAS_CRC32_IEEE`, which each
driver selects for the algorithms it implements. Applications can test them to choose an algorithm
the target can compute, or to fail the build with a clear message as the
:zephyr:code-sample:`crc_drivers` sample does.

.. _crc_driver_configuration:

Configuration
*************

The drivers are grouped under :kconfig:option:`CONFIG_CRC_DRIVER`. Each vendor driver, such as
:kconfig:option:`CONFIG_CRC_STM32` or :kconfig:option:`CONFIG_CRC_DRIVER_NXP`, defaults to ``y``
as soon as a matching devicetree node is enabled.

The sample and the driver test only enable :kconfig:option:`CONFIG_CRC`. When the ``zephyr,crc``
chosen node exists, :kconfig:option:`CONFIG_CRC_HW_HANDLER` defaults to ``y``, selects
:kconfig:option:`CONFIG_CRC_DRIVER`, and overrides the weak software implementations of the
subsystem functions with versions that run :c:func:`crc_begin`, :c:func:`crc_update` and
:c:func:`crc_finish` on the chosen device. Existing callers of :c:func:`crc32_ieee` and similar
functions thus use the hardware without changes. Setting :kconfig:option:`CONFIG_CRC_HW_HANDLER`
to ``n`` keeps the software implementations; :kconfig:option:`CONFIG_CRC_DRIVER` must then be
enabled explicitly to use the driver API directly.

Devicetree Configuration
************************

A CRC unit is described by a node with a vendor specific compatible such as
:dtcompatible:`st,stm32-crc`, :dtcompatible:`nxp,crc`, :dtcompatible:`renesas,ra-crc` or
:dtcompatible:`silabs,gpcrc`. The node carries the register block and, depending on the vendor,
``clocks`` and ``resets`` properties. SoC devicetree files define the node disabled. A board file
or application overlay enables it and, usually, designates it as the system CRC device through the
``zephyr,crc`` :ref:`chosen node <devicetree-chosen-nodes>`:

.. code-block:: devicetree
   :caption: Enabling the CRC unit and selecting it as the chosen CRC device

   / {
       chosen {
           zephyr,crc = &crc;
       };
   };

   &crc {
       status = "okay";
   };

The chosen node is how the CRC subsystem, the :zephyr:code-sample:`crc_drivers` sample and the
driver test in :zephyr_file:`tests/drivers/crc` find the device. Applications may also refer to the
node directly by label with :c:macro:`DEVICE_DT_GET`.

Typical application flow
************************

#. Get the CRC device, usually with ``DEVICE_DT_GET(DT_CHOSEN(zephyr_crc))``, and check it with
   :c:func:`device_is_ready`.
#. Declare a :c:struct:`crc_ctx` and initialize its type, polynomial, seed and reflection flags.
#. Call :c:func:`crc_begin`. The driver validates the context, waits for the unit to become free,
   programs the polynomial, seed and reflection settings, and marks the context
   :c:enumerator:`CRC_STATE_IN_PROGRESS`.
#. Call :c:func:`crc_update` for each buffer of the message, in order. The unit accumulates across
   calls, so a message split over several buffers gives the same result as one buffer holding the
   concatenation; the driver test checks this with two consecutive updates. Zero-length updates
   are accepted by some drivers and rejected by others, so skip empty buffers.
#. Call :c:func:`crc_finish`. The driver stores the checksum in :c:member:`crc_ctx.result`,
   returns the context to :c:enumerator:`CRC_STATE_IDLE` and releases the unit.
#. Read :c:member:`crc_ctx.result`, or compare it with an expected value using
   :c:func:`crc_verify`.

Basic Operation
***************

The following function, modeled on the :zephyr:code-sample:`crc_drivers` sample, computes the
CRC-32 checksum of a buffer on the chosen CRC device and checks it against a known value.

.. code-block:: c
   :caption: Computing and verifying a CRC-32 checksum

   static const struct device *const crc_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_crc));

   int check_crc32(const uint8_t *data, size_t len, uint32_t expected)
   {
       struct crc_ctx ctx = {
           .type = CRC32_IEEE,
           .polynomial = CRC32_IEEE_POLY,
           .seed = CRC32_IEEE_INIT_VAL,
           .reversed = CRC_FLAG_REVERSE_INPUT | CRC_FLAG_REVERSE_OUTPUT,
       };
       int ret;

       if (!device_is_ready(crc_dev)) {
           return -ENODEV;
       }

       ret = crc_begin(crc_dev, &ctx);
       if (ret != 0) {
           return ret;
       }

       ret = crc_update(crc_dev, &ctx, data, len);
       if (ret != 0) {
           return ret;
       }

       ret = crc_finish(crc_dev, &ctx);
       if (ret != 0) {
           return ret;
       }

       /* ctx.result now holds the checksum */
       return crc_verify(&ctx, expected);
   }

Concurrency and blocking behavior
*********************************

The API is synchronous: every function completes its step before returning, and there is no
callback or asynchronous variant. A driver may still use DMA internally. With
:kconfig:option:`CONFIG_CRC_STM32_DMA`, for example, the STM32 driver moves buffers of at least
:kconfig:option:`CONFIG_CRC_STM32_DMA_THRESHOLD` bytes into the unit through the DMA channel
listed in the node's ``dmas`` property, and :c:func:`crc_update` sleeps until the transfer is done.

A CRC unit holds a single computation at a time. The in-tree drivers serialize access with a
semaphore that :c:func:`crc_begin` takes without a timeout and :c:func:`crc_finish` gives back, so
a second thread calling :c:func:`crc_begin` during a computation blocks until the first one
finishes; the driver test exercises exactly this. The functions must therefore be called from
thread context, not from an interrupt handler, and a computation should be finished promptly.
Because the lock is a semaphore rather than a mutex, :c:func:`crc_begin` and :c:func:`crc_finish`
need not run on the same thread. Drivers that need a peripheral clock turn it on during
initialization and leave it on; none of the in-tree drivers implement device power management.

Error handling
==============

* :c:func:`crc_begin`, :c:func:`crc_update` and :c:func:`crc_finish` return ``-ENOSYS`` when the
  driver does not implement the corresponding operation.
* :c:func:`crc_begin` returns ``-ENOTSUP`` when the unit cannot compute the requested type, and
  ``-EINVAL`` when the polynomial or reflection flags do not fit the type or, in several drivers,
  when the context is not in :c:enumerator:`CRC_STATE_IDLE`. The unit is not reserved in these
  cases.
* :c:func:`crc_update` and :c:func:`crc_finish` return ``-EINVAL`` when the context is not in
  :c:enumerator:`CRC_STATE_IN_PROGRESS`, for example before :c:func:`crc_begin` or after
  :c:func:`crc_finish`.
* When :c:func:`crc_update` fails after a successful :c:func:`crc_begin`, for example because a
  driver requires buffer lengths that are a multiple of 4 bytes for 32-bit algorithms and returns
  ``-ENOTSUP``, the driver abandons the computation, returns the context to
  :c:enumerator:`CRC_STATE_IDLE` and releases the unit. Do not call :c:func:`crc_finish` in this
  case; start over with :c:func:`crc_begin` instead.
* :c:func:`crc_verify` returns ``-EBUSY`` while the context is still
  :c:enumerator:`CRC_STATE_IN_PROGRESS`, ``-EPERM`` when the result differs from the expected
  value, and ``0`` when they match.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_CRC_DRIVER`
* :kconfig:option:`CONFIG_CRC_DRIVER_INIT_PRIORITY`
* :kconfig:option:`CONFIG_CRC`
* :kconfig:option:`CONFIG_CRC_HW_HANDLER`
* :kconfig:option-regex:`CONFIG_CRC_DRIVER_HAS_.*`
* :kconfig:option:`CONFIG_CRC_STM32_DMA`
* :kconfig:option:`CONFIG_CRC_STM32_DMA_THRESHOLD`

API Reference
*************

.. doxygengroup:: crc_interface
