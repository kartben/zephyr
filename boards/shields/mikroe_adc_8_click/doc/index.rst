.. _mikroe_adc_8_click_shield:

MikroElektronika ADC 8 Click
============================

Overview
********

`ADC 8 Click`_ is a high precision, low-power, 16-bit analog-to-digital converter (ADC), based
around the ADS1115 IC. It is capable of sampling signals on four single-ended or two differential
input channels. Although the ADS1115 cannot use an external reference, it incorporates a low-drift
programmable voltage reference, along with the programmable gain amplifier (PGA). This allows for
great flexibility in terms of the input signal level: it can sample signals from ±256 mV, up to
6.144 V, allowing a very high precision for a wide range of input signals, making it an excellent
choice for various instrumentation applications.

.. figure:: images/mikroe_adc_8_click.webp
   :align: center
   :alt: ADC 8 Click
   :height: 300px

   ADC 8 Click

Requirements
************


This shield can only be used with a board that provides a mikroBUS |trade| socket and defines a
``mikrobus_i2c`` node label for the mikroBUS™ I2C interface. See :ref:`shields` for more details.

Programming
**********

Set ``-DSHIELD=mikroe_adc_8_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/sensor_shell
   :board: lpcxpresso55s16
   :shield: mikroe_adc_8_click
   :goals: build

This will build the :zephyr:code-sample:`sensor_shell` sample which provides a quick way to verify
the shield is working correctly. After flashing, you can use the ``sensor`` command to list
available sensors and read their values.

References
**********

- `ADC 8 Click`_
- `ADC 8 Click schematic`_

.. _ADC 8 Click: https://www.mikroe.com/adc-8-click
.. _ADC 8 Click schematic: https://download.mikroe.com/documents/add-on-boards/click/adc-8/ADC-8-click-schematic-v100.pdf
