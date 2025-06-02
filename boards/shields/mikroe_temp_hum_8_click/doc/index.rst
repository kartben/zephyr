.. _mikroe_temp_hum_8_click_shield:

MikroElektronika Temp Hum 8 Click
=================================

Overview
********

Temp&Hum 8 click is based on a sensor from the popular SHT family, designed to measure temperature
and humidity. This sensor family has already become an industry standard, providing proven
reliability and stability while requiring a minimum number of components, making the development of
applications cheaper and faster. The sensor used on this click is labeled as SHT21, which features a
high degree of measurement linearity, thanks to the factory calibration and testing which is
performed for each sensor sample. Featuring high accuracy, good linearity, proven reliability, and
long-term stability, you can use this sensor can for many different applications. Some of them
include weather stations, various environmental data collection applications, IoT based
applications, and all applications that require a reliable thermal and humidity readings over longer
periods of time.

.. figure:: images/mikroe_temp_hum_8_click.webp
   :align: center
   :alt: Temp Hum 8 Click
   :height: 300px

   Temp Hum 8 Click

Requirements
************


This shield can only be used with a board that provides a mikroBUS |trade| socket and defines a
``mikrobus_i2c`` node label for the mikroBUS™ I2C interface. See :ref:`shields` for more details.

Programming
**********

Set ``-DSHIELD=mikroe_temp_hum_8_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/sensor_shell
   :board: lpcxpresso55s16
   :shield: mikroe_temp_hum_8_click
   :goals: build

This will build the :zephyr:code-sample:`sensor_shell` sample which provides a quick way to verify
the shield is working correctly. After flashing, you can use the ``sensor`` command to list
available sensors and read their values.

References
**********

- `Temp Hum 8 Click`_
- `Temp Hum 8 Click schematic`_

.. _Temp Hum 8 Click: https://www.mikroe.com/temp-hum-8-click
.. _Temp Hum 8 Click schematic: https://download.mikroe.com/documents/add-on-boards/click/temphum-8/temp-hum-8-click-schematic-v100.pdf
