.. _adc_api:

Analog-to-Digital Converter (ADC)
#################################

Overview
********

The ADC (Analog-to-Digital Converter) API provides access to ADC hardware
peripherals. It allows an application to configure one or more channels, each
with their own gain, reference, and acquisition time settings, and then trigger
conversions either synchronously (blocking) or asynchronously.

Conversion results are expressed in raw ADC counts; the driver provides a
helper to convert raw counts to millivolts using the configured gain and
reference voltage.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_ADC`

API Reference
*************

.. doxygengroup:: adc_interface
