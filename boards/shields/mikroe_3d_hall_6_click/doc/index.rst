.. _mikroe_3d_hall_6_click_shield:

MikroElektronika 3D Hall 6 Click
================================

Overview
********

`3D Hall 6 Click`_ is a very accurate, magnetic field sensing Click board™, used to measure the
intensity of the magnetic field across three perpendicular axes. It is equipped with the MLX90380, a
monolithic contactless sensor IC sensitive to the flux density applied orthogonally and parallel to
the IC surface, from Melexis. This IC has a separate Hall sensing element on each axis, which allows
a very accurate and reliable measurement of the magnetic field intensity in a 3D space, offering a
basis for accurate positional calculations. The `3D Hall 6 Click`_ supports the industry-standard
SPI communication protocol for communicating with the main MCU.

.. figure:: images/mikroe_3d_hall_6_click.webp
   :align: center
   :alt: 3D Hall 6 Click
   :height: 300px

   3D Hall 6 Click

Requirements
************


This shield can only be used with a board that provides a mikroBUS |trade| socket and defines a
``mikrobus_spi`` node label for the mikroBUS™ SPI interface. See :ref:`shields` for more details.

Programming
**********

Set ``-DSHIELD=mikroe_3d_hall_6_click`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/sensor_shell
   :board: lpcxpresso55s16
   :shield: mikroe_3d_hall_6_click
   :goals: build

This will build the :zephyr:code-sample:`sensor_shell` sample which provides a quick way to verify
the shield is working correctly. After flashing, you can use the ``sensor`` command to list
available sensors and read their values.

References
**********

- `3D Hall 6 Click`_

.. _3D Hall 6 Click: https://www.mikroe.com/3d-hall-6-click
