.. _adc_api:

Analog-to-Digital Converter (ADC)
#################################

Overview
********

An Analog-to-Digital Converter (ADC) converts analog signals (continuous voltage levels) into
digital values that can be processed by software. ADCs are essential for interfacing with
analog sensors such as temperature sensors, light sensors, and voltage monitors.

The ADC API provides a generic interface for ADC peripherals. Key features include:

**Channel Configuration**
  Configure multiple independent ADC channels with different settings.
  Support for various gain factors, reference voltages, and acquisition times.

**Sampling Modes**
  Single-shot sampling for on-demand readings.
  Continuous and sequence sampling for multiple channels.
  Asynchronous sampling with callback notification.

**Resolution and Accuracy**
  Configurable resolution (8, 10, 12, 16 bits, etc.).
  Oversampling support for improved accuracy.

**RTIO Integration**
  Support for Real-Time I/O (RTIO) operations for efficient data acquisition.
  Integration with DMA for high-speed continuous sampling.

Devicetree Configuration
************************

ADC controllers are defined in the Devicetree as nodes with an ``adc-controller`` property.
Individual ADC channels are configured as child nodes or referenced through ``io-channels``.

Example of an ADC controller definition:

.. code-block:: devicetree

   adc0: adc@40022000 {
       compatible = "st,stm32-adc";
       reg = <0x40022000 0x100>;
       interrupts = <18 0>;
       #io-channel-cells = <1>;
       status = "okay";

       channel@1 {
           reg = <1>;
           zephyr,gain = "ADC_GAIN_1";
           zephyr,reference = "ADC_REF_INTERNAL";
           zephyr,acquisition-time = <ADC_ACQ_TIME_DEFAULT>;
           zephyr,resolution = <12>;
       };
   };

Example of referencing ADC channels in an application node:

.. code-block:: devicetree

   / {
       zephyr,user {
           io-channels = <&adc0 1>;
       };
   };

Basic Operation
***************

ADC operations typically involve configuring channels, setting up a read sequence,
and performing synchronous or asynchronous reads.

.. code-block:: c
   :caption: Configure and read from an ADC channel

   #include <zephyr/drivers/adc.h>

   #define ADC_NODE DT_NODELABEL(adc0)

   static const struct device *adc_dev = DEVICE_DT_GET(ADC_NODE);

   void setup_adc(void)
   {
       struct adc_channel_cfg channel_cfg = {
           .gain = ADC_GAIN_1,
           .reference = ADC_REF_INTERNAL,
           .acquisition_time = ADC_ACQ_TIME_DEFAULT,
           .channel_id = 0,
       };
       struct adc_sequence sequence = {
           .channels = BIT(0),
           .buffer = &sample_buffer,
           .buffer_size = sizeof(sample_buffer),
           .resolution = 12,
       };
       int16_t sample_buffer;
       int ret;

       if (!device_is_ready(adc_dev)) {
           printk("ADC device not ready\n");
           return;
       }

       /* Configure the channel */
       ret = adc_channel_setup(adc_dev, &channel_cfg);
       if (ret < 0) {
           printk("Failed to configure ADC channel: %d\n", ret);
           return;
       }

       /* Read the ADC */
       ret = adc_read(adc_dev, &sequence);
       if (ret < 0) {
           printk("Failed to read ADC: %d\n", ret);
           return;
       }

       printk("ADC reading: %d\n", sample_buffer);
   }

Refer to :zephyr:code-sample:`adc_dt` for a complete example of using the ADC API.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_ADC`
* :kconfig:option:`CONFIG_ADC_SHELL`
* :kconfig:option:`CONFIG_ADC_ASYNC`
* :kconfig:option:`CONFIG_ADC_CONFIGURABLE_INPUTS`

API Reference
*************

.. doxygengroup:: adc_interface
