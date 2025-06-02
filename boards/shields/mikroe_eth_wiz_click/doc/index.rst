.. _mikroe_eth_wiz_click_shield:

MikroElektronika ETH WIZ Click
==============================

Overview
********

`ETH WIZ Click`_ carries W5500, a 48-pin, 10/100 BASE-TX standalone ethernet controller with a
hardwired TCP/IP Internet protocol offload engine, along with a standard RJ-45 connector.

Requirements
************


This shield can only be used with a board that provides a mikroBUS |trade| socket and defines a
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

- `ETH WIZ Click`_

.. _ETH WIZ Click: https://www.mikroe.com/eth-wiz-click
