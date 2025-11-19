.. _adc_api:

Analog-to-Digital Converter (ADC)
#################################

Overview
********

An Analog-to-Digital Converter (ADC) samples an analog signal and produces a digital value that can
be processed by software.

The ADC API provides a generic method to interact with ADC peripherals. It allows applications to
configure :ref:`adc_channels`, trigger single or multiple conversions, and retrieve raw samples that
can be converted to physical units (typically voltages). Key concepts include:

**ADC Channels**
  Each ADC channel represents a conversion path from an analog signal (external pin or internal
  source) to a digital sample. Channels are configured independently.

**Conversion Sequence**
  Conversions are requested using so called sequences that describe which channels to sample, the
  resolution and oversampling settings, and the buffer to store the results.

**Devicetree Integration**
  ADCs and their channels are typically defined in the Devicetree, allowing drivers and applications
  to reference them in a hardware-agnostic way using helper macros and :c:struct:`adc_dt_spec`
  structure.

.. _adc_channels:

ADC Channels
************

In Zephyr, an ADC channel is identified by a zero-based channel index. The mapping between this
index and the physical signal (pin or internal source) is SoC- and board-specific and is described
by the Devicetree.

Each channel is configured with :c:struct:`adc_channel_cfg` and is selected for sampling through an
:c:struct:`adc_sequence`. A single :c:func:`adc_read` call can sample one or several channels from
the same ADC instance.

* Channels are identified by their index in the ADC instance. The same index value on different ADC
  peripherals (for example, ``ADC0`` vs. ``ADC1``) refers to different hardware channels.
* Some drivers expose only a subset of indices supported by the hardware. Attempting to configure a
  channel that is not implemented will return an error.
* On SoCs with differential inputs, a logical channel index may refer to multiple pins
  (positive/negative pair); the actual pins are described by Devicetree properties and
  :c:struct:`adc_channel_cfg` fields.

Each channel must be configured with :c:func:`adc_channel_setup` (or :c:func:`adc_channel_setup_dt`
for Devicetree-based channels) before it can be sampled. See :c:struct:`adc_channel_cfg` for a
description of the available parameters (gain, reference, acquisition time, differential mode, etc.)
and the ``ADC_ACQ_TIME`` macro family for composing acquisition time values.

Sampling
========

Once channels are configured, sampling is controlled by an :c:struct:`adc_sequence` structure.
The typical workflow is:

1. Configure each channel once with :c:func:`adc_channel_setup`.
2. Fill in an :c:struct:`adc_sequence` with the desired channels bitmask, resolution, and a buffer.
3. Call :c:func:`adc_read` (or :c:func:`adc_read_dt`) on the ADC device.

A single :c:func:`adc_read` call can sample multiple channels and, when combined with
:c:struct:`adc_sequence_options`, can perform multiple samplings at a given interval. The required
buffer size in bytes is:

.. code-block:: c

   buffer_size = number_of_channels * number_of_samples * sizeof(sample_type);

Where:

* ``number_of_channels`` is the number of bits set in :c:member:`adc_sequence.channels`.
* ``number_of_samples`` is ``1 + extra_samplings`` (see :c:member:`adc_sequence_options.extra_samplings`).
* ``sizeof(sample_type)`` is typically 2 bytes (``int16_t`` or ``uint16_t``).

Samples are stored in the buffer in channel index order (lowest to highest). When
:c:member:`adc_sequence.options` is ``NULL``, the sequence consists of a single sampling.

.. note::
   When ``extra_samplings`` is used, the delay between samples is controlled by
   :c:member:`adc_sequence_options.interval_us`. This delay is typically implemented using the
   kernel system timer, which may have limited precision. For high-frequency precise sampling,
   hardware-specific timers or DMA modes should be considered.

Converting raw values to voltages
=================================

To convert raw ADC values to physical voltages, use :c:func:`adc_raw_to_millivolts_dt` (or
:c:func:`adc_raw_to_millivolts` when not using :c:struct:`adc_dt_spec`). A
:c:func:`adc_raw_to_microvolts_dt` variant is also available for higher precision.

When the channel uses :c:enumerator:`ADC_REF_INTERNAL` as its reference, the reference voltage in
millivolts can be obtained with :c:func:`adc_ref_internal`.

.. important::
   For **differential** channels, the raw sample is a signed value. You must cast appropriately
   before converting:

   .. code-block:: c

      if (channel_cfg.differential) {
          val_mv = (int32_t)((int16_t)buf);
      } else {
          val_mv = (int32_t)buf;
      }

   The ``_dt`` conversion helpers handle the resolution adjustment (``resolution - 1``) for
   differential channels automatically.

Calibration
===========

Some ADC hardware supports calibration before a reading. Set :c:member:`adc_sequence.calibrate` to
``true`` to request calibration. Drivers that do not support calibration will ignore this flag.

Devicetree Configuration
************************

ADC controllers and their channels are described in the Devicetree. A typical ADC controller node
contains a ``#io-channel-cells`` property that specifies how channels are described when they are
referenced from other nodes.

Example of an ADC controller definition:

.. code-block:: devicetree

   lpadc0: lpadc@400af000 {
       compatible = "nxp,lpc-lpadc";
       reg = <0x400af000 0x1000>;
       #io-channel-cells = <1>;
   };

Example of referencing an ADC channel from a consumer node:

.. code-block:: devicetree

   temperature-sensor {
       compatible = "nxp,lpadc-temp40";
       io-channels = <&lpadc0 0>;
   };

In this example, channel ``0`` of ``lpadc0`` is used to sample the temperature signal.

Basic Operation
***************

ADC operations are usually performed on an :c:struct:`adc_dt_spec` structure, which is a container
for the ADC device and channel information specified in the Devicetree.

This structure is typically populated using :c:macro:`ADC_DT_SPEC_GET` (or any of its variants).

.. code-block:: c
   :caption: Populating an adc_dt_spec structure for an ADC channel defined as alias
             ``temp_sensor``

   #define TEMP_NODE DT_ALIAS(temp_sensor)
   static const struct adc_dt_spec temp_adc = ADC_DT_SPEC_GET(TEMP_NODE);

The :c:struct:`adc_dt_spec` structure can then be used to perform ADC operations.

.. code-block:: c
   :caption: Configure an ADC channel and read a single sample

   int err;
   uint16_t buf;
   int32_t val_mv;
   struct adc_sequence sequence = {
       .buffer      = &buf,
       .buffer_size = sizeof(buf),
   };

   if (!adc_is_ready_dt(&temp_adc)) {
       return -ENODEV;
   }

   err = adc_channel_setup_dt(&temp_adc);
   if (err < 0) {
       return err;
   }

   err = adc_sequence_init_dt(&temp_adc, &sequence);
   if (err < 0) {
       return err;
   }

   err = adc_read_dt(&temp_adc, &sequence);
   if (err < 0) {
       return err;
   }

   val_mv = (int32_t)buf;
   err = adc_raw_to_millivolts_dt(&temp_adc, &val_mv);
   if (err < 0) {
       /* Conversion to mV not supported, val_mv is unchanged (still raw) */
   } else {
       /* val_mv now contains the voltage in millivolts */
   }

ADC operations can also be performed directly on an ADC controller device and explicit channel
configuration, using :c:func:`adc_channel_setup` and :c:func:`adc_read` without
:c:struct:`adc_dt_spec`. See the :zephyr:code-sample:`adc_sequence` sample for an example of this
approach.

Asynchronous Reads
==================

When :kconfig:option:`CONFIG_ADC_ASYNC` is enabled, :c:func:`adc_read_async` (or
:c:func:`adc_read_async_dt`) can be used to start a conversion without blocking. A
``struct k_poll_signal`` is provided to be signaled when the conversion completes:

.. code-block:: c

   struct k_poll_signal async_sig = K_POLL_SIGNAL_INITIALIZER(async_sig);
   struct k_poll_event async_evt =
       K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &async_sig);

   err = adc_read_async_dt(&temp_adc, &sequence, &async_sig);
   if (err < 0) {
       return err;
   }

   /* Do other work... */

   k_poll(&async_evt, 1, K_FOREVER);
   /* Samples are now in the buffer */

DMA Support
***********

Some ADC drivers support Direct Memory Access (DMA) for efficient data transfers. When enabled, DMA
allows the ADC hardware to transfer samples directly to memory without CPU intervention, reducing
overhead and improving performance for high-speed or continuous sampling applications.

DMA support is driver-specific and typically requires:

1. Enabling the appropriate Kconfig option (for example, :kconfig:option:`CONFIG_ADC_STM32_DMA` for
   STM32 devices).
2. Configuring the ``dmas`` property in the Devicetree to associate a DMA channel with the ADC
   peripheral.

Example Devicetree configuration for an STM32 ADC with DMA:

.. code-block:: devicetree

   &adc1 {
       dmas = <&dma1 1 0 (STM32_DMA_PERIPH_RX | STM32_DMA_MEM_16BITS |
                          STM32_DMA_PERIPH_16BITS)>;
       dma-names = "adc";
   };

When DMA is enabled, the ADC driver automatically uses DMA transfers for :c:func:`adc_read`
operations. The application code remains unchanged; the driver handles DMA setup and completion
internally.

Streaming (RTIO)
****************

When :kconfig:option:`CONFIG_ADC_STREAM` is enabled, the ADC driver can continuously stream samples
using the :ref:`RTIO (Real-Time I/O) <rtio>` framework. This is suited for high-throughput or
continuous-sampling applications where polling with :c:func:`adc_read` would be too costly or too
slow.

The streaming workflow uses three main components:

1. **An RTIO IO device**, defined with the :c:macro:`ADC_DT_STREAM_IODEV` macro. This associates
   an ADC controller, a set of :c:struct:`adc_dt_spec` channels, and one or more
   :c:struct:`adc_stream_trigger` entries that describe when the ADC should produce data (for
   example, when the FIFO is full or reaches a watermark).

2. **An RTIO context with a memory pool**, defined with :c:macro:`RTIO_DEFINE_WITH_MEMPOOL`.
   The driver writes sample data into this pool; the application consumes it asynchronously.

3. **A decoder** (:c:struct:`adc_decoder_api`), obtained at runtime with
   :c:func:`adc_get_decoder`, used to extract individual frames and channels from the raw buffer.

A minimal streaming loop looks like this:

.. code-block:: c

   /* Define triggers */
   ADC_DT_STREAM_IODEV(iodev, DT_ALIAS(adc0), adc_channels,
       {ADC_TRIG_FIFO_FULL, ADC_STREAM_DATA_INCLUDE});

   /* Define RTIO context with memory pool */
   RTIO_DEFINE_WITH_MEMPOOL(adc_ctx, 16, 16, 20, 256, sizeof(void *));

   /* Start streaming */
   adc_stream(&iodev, &adc_ctx, NULL, &handles);

   while (1) {
       struct rtio_cqe *cqe = rtio_cqe_consume_block(&adc_ctx);
       uint8_t *buf;
       uint32_t buf_len;

       rtio_cqe_get_mempool_buffer(&adc_ctx, cqe, &buf, &buf_len);
       rtio_cqe_release(&adc_ctx, cqe);

       /* Decode and process samples from buf using adc_decoder_api */

       rtio_release_buffer(&adc_ctx, buf, buf_len);
   }

See the :zephyr:code-sample:`adc_stream` sample for a complete working example.

ADC Shell
*********

When :kconfig:option:`CONFIG_ADC_SHELL` is enabled, ADC channels can be read interactively from the
:ref:`shell <shell_api>` using the ``adc`` command. This is useful for quick hardware validation
without writing application code.

Configuration Options
*********************

Main configuration options:

* :kconfig:option:`CONFIG_ADC`
* :kconfig:option:`CONFIG_ADC_ASYNC`
* :kconfig:option:`CONFIG_ADC_STREAM`
* :kconfig:option:`CONFIG_ADC_SHELL`

API Reference
*************

.. doxygengroup:: adc_interface
