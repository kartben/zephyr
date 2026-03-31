.. _mikroe_rng90_click_shield:

MikroElektronika RNG90 Click
============================

Overview
********

`RNG90 Click`_ boards carry the Microchip RNG90 CryptoAuthentication secure
random number generator with an I2C interface (fixed 7-bit address ``0x40``).
The Zephyr driver is described in the :dtcompatible:`microchip,rng90` binding.

Requirements
************

This shield can only be used with a board that provides a mikroBUS™ socket and
defines a ``mikrobus_i2c`` node label for the mikroBUS™ I2C interface. See
:ref:`shields` for more details.

Boards that already enable an on-SoC entropy source may need that driver
disabled so ``zephyr,entropy`` from this shield is the only entropy backend
selected (see :ref:`kconfig` for your SoC entropy option).

Programming
***********

Set ``--shield mikroe_rng90_click`` when you invoke ``west build``. The shield
enables :kconfig:option:`CONFIG_ENTROPY_MICROCHIP_RNG90` via
``mikroe_rng90_click.conf``.

References
**********

.. target-notes::

.. _RNG90 Click: https://www.mikroe.com/rng-2-click
