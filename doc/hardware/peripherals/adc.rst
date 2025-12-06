.. _adc_api:

Analog-to-Digital Converter (ADC)
#################################

Overview
********

An Analog-to-Digital Converter (ADC) samples an analog signal and produces a digital value that
can be processed by software.

The ADC API provides a generic method to interact with ADC peripherals. It allows applications to
configure :ref:`adc_channels`, trigger single or multiple conversions, and retrieve raw samples that
can be converted to physical units (typically voltages). Key concepts include:

**ADC Channels**
  Each ADC channel represents a conversion path from an analog signal (external pin or internal
  source) to a digital sample. Channels are configured independently and can be sampled
  individually or as part of a multi-channel sequence.

**Sampling Configuration**
  Applications can select the resolution, reference, gain, and acquisition time for conversions,
  subject to the capabilities of the underlying ADC hardware.

**Sequences and Triggers**
  Conversions are requested using sequences that describe which channels to sample, where to store
  the results, and how many samples to take. Some drivers support hardware triggering and
  asynchronous operation.

**Devicetree Integration**
  ADCs and their channels are typically defined in the Devicetree, allowing drivers and
  applications to reference them in a hardware-agnostic way using helper macros and
  :c:struct:`adc_dt_spec` structure.

.. _adc_channels:

ADC Channels
************

In Zephyr, an ADC channel is identified by a zero-based channel index. The mapping between this
index and the physical signal (pin or internal source) is SoC- and board-specific and is
described by the Devicetree.

Each channel is configured with :c:struct:`adc_channel_cfg` and is selected for sampling through
an :c:struct:`adc_sequence`. A single :c:func:`adc_read` call can sample one or several channels
from the same ADC instance.

Channel numbering
=================

* Channels are identified by their index in the ADC instance. The same index value on different
  ADC peripherals (for example, ``ADC0`` vs. ``ADC1``) refers to different hardware channels.
* Some drivers expose only a subset of indices supported by the hardware. Attempting to
  configure a channel that is not implemented will return an error.
* On SoCs with differential inputs, a logical channel index may refer to a positive/negative pair;
  the actual pins are described by Devicetree properties and :c:struct:`adc_channel_cfg` fields.

Channel configuration
=====================

The :c:struct:`adc_channel_cfg` structure controls how the ADC samples a given channel. The most
relevant parameters are:

* **Reference:** internal or external reference voltage used to convert the raw value to a
  physical voltage (:c:member:`adc_channel_cfg.reference`).
* **Gain:** hardware gain applied before conversion, used when computing the effective input
  voltage (:c:member:`adc_channel_cfg.gain`).
* **Acquisition time:** minimum time the sampler must track the input signal before the
  conversion starts; drivers may round this to the closest supported value
  (:c:member:`adc_channel_cfg.acquisition_time`).
* **Single-ended vs. differential:** whether the channel measures against ground or between two
  input pins (:c:member:`adc_channel_cfg.differential`).

The exact set of supported parameters depends on the driver. Unsupported combinations will
cause :c:func:`adc_channel_setup`/:c:func:`adc_channel_setup_dt` to fail with an error code.

Sampling from channels
======================

Sampling is controlled by an :c:struct:`adc_sequence` structure:

* :c:member:`adc_sequence.channels` is a bitmask of channel indices to sample.
* :c:member:`adc_sequence.buffer` and :c:member:`adc_sequence.buffer_size` point to a buffer large
  enough for all samples.
* Resolution, oversampling, and sampling frequency are set according to the application requirements
  and driver capabilities.

To sample from one or more channels:

1. Configure each channel once with :c:func:`adc_channel_setup`.
2. Initialize an :c:struct:`adc_sequence` with the desired channels and buffer.
3. Call :c:func:`adc_read` (or :c:func:`adc_read_async` when supported) on the ADC device.

Samples are stored in the buffer in channel index order (from lowest to highest index). For
multi-channel sequences, this means that the first sample corresponds to the lowest-numbered
channel in the bitmask, the second to the next channel, and so on.

Converting raw values to voltages
=================================

The ADC hardware returns integer sample values. To convert them to voltages, you need the channel
configuration (reference, gain, resolution) and any board-level scaling.

For ADC channels defined in Devicetree, the helper functions :c:func:`adc_raw_to_millivolts` and
:c:func:`adc_raw_to_millivolts_dt` can be used to perform this conversion in a driver-independent
way.

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
:c:struct:`adc_dt_spec`.

DMA Support
***********

Some ADC drivers support Direct Memory Access (DMA) for efficient data transfers. When enabled,
DMA allows the ADC hardware to transfer samples directly to memory without CPU intervention,
reducing overhead and improving performance for high-speed or continuous sampling applications.

DMA support is driver-specific and typically requires:

1. Enabling the appropriate Kconfig option (for example,
   :kconfig:option:`CONFIG_ADC_STM32_DMA` for STM32 devices).
2. Configuring the ``dmas`` property in the Devicetree to associate a DMA channel
   with the ADC peripheral.

Example Devicetree configuration for an STM32 ADC with DMA:

.. code-block:: devicetree

   &adc1 {
       dmas = <&dma1 1 0 (STM32_DMA_PERIPH_RX | STM32_DMA_MEM_16BITS |
                         STM32_DMA_PERIPH_16BITS)>;
       dma-names = "adc";
   };

When DMA is enabled, the ADC driver automatically uses DMA transfers for :c:func:`adc_read`
operations. The application code remains unchanged; the driver handles DMA setup and
completion internally.

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
