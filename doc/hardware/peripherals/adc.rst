.. _adc_api:

Analog-to-Digital Converter (ADC)
#################################

Overview
********

An Analog-to-Digital Converter (ADC) is a peripheral that converts a continuous
analog voltage into a discrete digital value. ADCs are commonly used to measure
sensor outputs, battery voltages, and other real-world signals.

The Zephyr ADC API provides a hardware-agnostic interface for configuring ADC
channels and performing analog readings. Key concepts include:

**Channels**
  An ADC peripheral typically has multiple input channels, each connected to
  a different analog pin. Each channel must be configured before it can be
  sampled. The channel configuration includes gain, voltage reference, and
  acquisition time settings.

**Resolution**
  The number of bits used to represent the converted value. A 12-bit ADC
  produces values in the range 0--4095 for single-ended channels, or
  -2048--2047 for differential channels.

**Gain and Reference Voltage**
  The gain amplifies (or attenuates) the input signal before conversion. The
  reference voltage defines the full-scale input range. Together, they
  determine the mapping from input voltage to raw digital value. The API
  provides :c:func:`adc_raw_to_millivolts` and :c:func:`adc_raw_to_microvolts`
  to convert raw values back to physical voltages.

**Oversampling**
  Averaging multiple conversions to reduce noise. When oversampling is set to
  *N*, each reported sample is the average of 2\ :sup:`N` conversions.

**Single-Ended vs. Differential**
  Single-ended channels measure voltage between an input pin and ground.
  Differential channels measure the voltage difference between two input pins,
  which allows reading bipolar signals.

The API supports three modes of operation:

1. **Synchronous (polling):** :c:func:`adc_read` blocks until the sampling
   sequence completes.
2. **Asynchronous:** :c:func:`adc_read_async` returns immediately and signals
   completion via a :c:struct:`k_poll_signal`
   (requires :kconfig:option:`CONFIG_ADC_ASYNC`).
3. **Streaming:** Continuous data acquisition using the RTIO subsystem
   (requires :kconfig:option:`CONFIG_ADC_STREAM`).

Devicetree Configuration
*************************

ADC channels are typically described in the Devicetree. A channel node is
defined as a child of its ADC controller, with properties for gain, reference,
acquisition time, and input pin selection:

.. code-block:: devicetree

   &adc0 {
       #address-cells = <1>;
       #size-cells = <0>;

       channel@0 {
           reg = <0>;
           zephyr,gain = "ADC_GAIN_1_6";
           zephyr,reference = "ADC_REF_INTERNAL";
           zephyr,acquisition-time = <ADC_ACQ_TIME_DEFAULT>;
           zephyr,resolution = <12>;
           zephyr,input-positive = <NRF_SAADC_AIN6>;
       };
   };

Consumer nodes (such as the application) reference ADC channels through the
``io-channels`` property:

.. code-block:: devicetree

   / {
       zephyr,user {
           io-channels = <&adc0 0>, <&adc0 1>;
           io-channel-names = "voltage", "current";
       };
   };

Basic Operation
***************

The recommended workflow uses Devicetree to obtain channel specifications and
the ``_dt`` convenience functions:

1. **Get channel specs** from the Devicetree using :c:macro:`ADC_DT_SPEC_GET`
   (or :c:macro:`ADC_DT_SPEC_GET_BY_IDX`, :c:macro:`ADC_DT_SPEC_GET_BY_NAME`):

   .. code-block:: c

      static const struct adc_dt_spec adc_chan =
          ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

2. **Verify the device is ready** and **configure the channel**:

   .. code-block:: c

      if (!adc_is_ready_dt(&adc_chan)) {
          return -ENODEV;
      }

      err = adc_channel_setup_dt(&adc_chan);
      if (err < 0) {
          return err;
      }

3. **Perform a reading** by initializing a sequence and calling
   :c:func:`adc_read_dt`:

   .. code-block:: c

      int16_t buf;
      struct adc_sequence sequence = {
          .buffer = &buf,
          .buffer_size = sizeof(buf),
      };

      adc_sequence_init_dt(&adc_chan, &sequence);

      err = adc_read_dt(&adc_chan, &sequence);
      if (err < 0) {
          return err;
      }

4. **Convert the raw value to millivolts**:

   .. code-block:: c

      int32_t val_mv = (int32_t)buf;

      adc_raw_to_millivolts_dt(&adc_chan, &val_mv);

Refer to :zephyr:code-sample:`adc_dt` for a complete working example using
the Devicetree-based API. The :zephyr:code-sample:`adc_sequence` sample
demonstrates multi-sample sequences, and :zephyr:code-sample:`adc_stream`
shows asynchronous streaming with RTIO.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_ADC`
* :kconfig:option:`CONFIG_ADC_ASYNC`
* :kconfig:option:`CONFIG_ADC_STREAM`
* :kconfig:option:`CONFIG_ADC_SHELL`

API Reference
*************

.. doxygengroup:: adc_interface
