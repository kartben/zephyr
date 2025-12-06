.. _adc_api:

Analog-to-Digital Converter (ADC)
#################################

Overview
********

The ADC API allows applications to configure ADC channels, trigger conversions, and retrieve
samples that can be converted to voltages. ADCs and channels are defined in Devicetree and
accessed using :c:struct:`adc_dt_spec`.

.. _adc_channels:

Channels and Sequences
**********************

Channels are identified by a zero-based index and configured with :c:struct:`adc_channel_cfg`,
which specifies reference voltage, gain, acquisition time, and single-ended vs. differential mode.

Sampling uses :c:struct:`adc_sequence` to specify which channels to sample (as a bitmask) and
where to store results. Samples are stored in channel index order (lowest to highest).

Basic Usage
***********

.. code-block:: c

   #define TEMP_NODE DT_ALIAS(temp_sensor)
   static const struct adc_dt_spec adc = ADC_DT_SPEC_GET(TEMP_NODE);

   int err;
   uint16_t buf;
   int32_t val_mv;
   struct adc_sequence sequence = {
       .buffer      = &buf,
       .buffer_size = sizeof(buf),
   };

   if (!adc_is_ready_dt(&adc)) {
       return -ENODEV;
   }

   err = adc_channel_setup_dt(&adc);
   if (err < 0) {
       return err;
   }

   adc_sequence_init_dt(&adc, &sequence);

   err = adc_read_dt(&adc, &sequence);
   if (err < 0) {
       return err;
   }

   val_mv = (int32_t)buf;
   err = adc_raw_to_millivolts_dt(&adc, &val_mv);
   /* err < 0 means conversion not supported; val_mv contains raw value */

DMA Support
***********

Some drivers support DMA for efficient high-speed sampling. Enable the driver-specific Kconfig
(e.g., :kconfig:option:`CONFIG_ADC_STM32_DMA`) and configure the ``dmas`` property in Devicetree:

.. code-block:: devicetree

   &adc1 {
       dmas = <&dma1 1 0 (STM32_DMA_PERIPH_RX | STM32_DMA_MEM_16BITS |
                         STM32_DMA_PERIPH_16BITS)>;
       dma-names = "adc";
   };

Configuration Options
*********************

* :kconfig:option:`CONFIG_ADC`
* :kconfig:option:`CONFIG_ADC_ASYNC`
* :kconfig:option:`CONFIG_ADC_STREAM`
* :kconfig:option:`CONFIG_ADC_SHELL`

API Reference
*************

.. doxygengroup:: adc_interface
