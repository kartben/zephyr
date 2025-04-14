.. _mikroe_eth_wiz_click:

MikroElektronika ETH-WIZ Click
==============================

Overview
********

The ETH-WIZ Click shield carries a ETH-WIZ board from MikroElektronika.

Requirements
************

This shield can only be used with a board that provides a mikroBUS™ socket and defines a
``mikrobus_spi`` node label for the mikroBUS™ SPI interface. See :ref:`shields` for more details.

Programming
**********

Set ``-DSHIELD=mikroe_eth_wiz_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/sensor_shell
   :board: lpcxpresso55s16
   :shield: mikroe_eth_wiz_click
   :goals: build

This will build the :zephyr:code-sample:`sensor_shell` sample which provides a quick way to verify
the shield is working correctly. After flashing, you can use the ``sensor`` command to list
available sensors and read their values.

References
**********

- `ETH-WIZ Click webpage`_

.. _ETH-WIZ Click webpage: https://www.mikroe.com/eth-wiz-click
