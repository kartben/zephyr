.. _adc_api:

Analog-to-Digital Converter (ADC)
#################################

Overview
********

An Analog-to-Digital Converter (ADC) converts analog voltage signals into digital values
that can be processed by software.

ADC Channels
============

An ADC channel is a logical entity that represents a configured ADC sampling path. Each channel
is associated with one or more physical analog input pins and has specific configuration
properties such as gain, reference voltage, and acquisition time.

Channels can be:

* **Single-ended**: Measure voltage relative to a reference (typically ground)
* **Differential**: Measure voltage difference between two analog inputs

The channel identifier (0-31) is used by the API to reference the channel in read operations.
Depending on the hardware, the channel identifier may directly correspond to a physical input
pin, or the physical input(s) may be explicitly specified using the ``input_positive`` and
``input_negative`` configuration parameters.

The ADC API provides methods to configure and read from ADC channels. Key features include:

**Channel Configuration**
  Configure channels with specific gain, reference voltage, and acquisition time settings.
  Support for single-ended and differential channels.

**Data Acquisition**
  Read samples from one or multiple channels.
  Support for synchronous and asynchronous (RTIO-based) read operations.

**Devicetree Integration**
  ADC channels are typically defined in the Devicetree, allowing applications to reference
  them using :c:struct:`adc_dt_spec`.

Devicetree Configuration
************************

ADC controllers are defined in the Devicetree with channel configurations as child nodes.
The controller node specifies the ADC hardware instance, while child nodes define each
channel's properties including which physical input pin(s) to use and the channel's
electrical characteristics.

Example of an ADC controller with channel configuration:

.. code-block:: devicetree

   &adc {
       #address-cells = <1>;
       #size-cells = <0>;

       channel@0 {
           reg = <0>;
           zephyr,gain = "ADC_GAIN_1_6";
           zephyr,reference = "ADC_REF_INTERNAL";
           zephyr,acquisition-time = <ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 10)>;
           zephyr,input-positive = <NRF_SAADC_AIN1>;
       };
   };

Example of referencing an ADC channel in a custom node:

.. code-block:: devicetree

   / {
       zephyr,user {
           io-channels = <&adc 0>;
       };
   };

Basic Operation
***************

ADC operations are typically performed on a :c:struct:`adc_dt_spec` structure, which
contains the channel information from the Devicetree.

The structure is populated using :c:macro:`ADC_DT_SPEC_GET` or related macros.

.. code-block:: c
   :caption: Obtaining ADC channel specification from Devicetree

   #include <zephyr/drivers/adc.h>

   #define ADC_NODE DT_PATH(zephyr_user)
   static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(ADC_NODE);

After obtaining the channel specification, configure the channel and read samples:

.. code-block:: c
   :caption: Configuring and reading from an ADC channel

   int err;
   uint16_t buf;
   struct adc_sequence sequence = {
       .buffer = &buf,
       .buffer_size = sizeof(buf),
   };

   err = adc_channel_setup_dt(&adc_channel);
   if (err < 0) {
       return err;
   }

   (void)adc_sequence_init_dt(&adc_channel, &sequence);

   err = adc_read(adc_channel.dev, &sequence);
   if (err < 0) {
       return err;
   }

Refer to :zephyr:code-sample:`adc_dt` for a complete example of reading ADC channels
using the :c:struct:`adc_dt_spec` structure.

Configuration Options
*********************

Main configuration options:

* :kconfig:option:`CONFIG_ADC`
* :kconfig:option:`CONFIG_ADC_ASYNC`
* :kconfig:option:`CONFIG_ADC_CONFIGURABLE_INPUTS`
* :kconfig:option:`CONFIG_ADC_SHELL`

API Reference
*************

.. doxygengroup:: adc_interface
