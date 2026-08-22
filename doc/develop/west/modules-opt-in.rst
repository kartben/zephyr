.. _west-modules-opt-in:

Opt-in module fetching
######################

The default :file:`west.yml` manifest still downloads every active west
project when you run ``west update``. Most of those projects are vendor
HALs and other modules that a typical application never uses.

This page describes an **opt-in** workflow that fetches only the
projects a given *build* actually needs. A board name is just the
query; the dependencies themselves live on the SoC, the driver, the
binding, the shield, or the application.

Default ``west update`` behavior is unchanged.

.. _west-modules-cmd:

``west modules``
****************

.. code-block:: console

   # SoC HAL only (hello_world on a Nucleo has no extra sensors)
   west modules list -b nucleo_f401re

   # Shield DTS pulls in ST sensor drivers, which depend on hal_st
   west modules list -b nucleo_f401re --shield x_nucleo_iks01a3

   # Application tests.yaml / overlays / prj.conf can add more
   west modules fetch -b qemu_x86 --app samples/modules/lvgl/demos

   west modules fetch --dry-run -b qemu_x86
   west modules check -b nrf52840dk/nrf52840

A fresh workspace can look like this:

.. code-block:: console

   west init
   west modules fetch -b nucleo_f401re
   west build -b nucleo_f401re samples/hello_world

``west build --fetch-modules`` (or ``west config zephyr.fetch-modules true``)
runs the same resolution before CMake.

West is optional. The resolver is :zephyr_file:`scripts/list_modules.py`.

Where dependencies are declared
*******************************

Declare a module on the thing that **uses** it.

Driver / subsystem Kconfig
==========================

This is the existing pattern, and it is enough for fetch-time
resolution. No extra YAML is required:

.. code-block:: kconfig

   menuconfig LSM6DSO
           bool "LSM6DSO I2C/SPI accelerometer and gyroscope Chip"
           default y
           depends on DT_HAS_ST_LSM6DSO_ENABLED || DT_HAS_ST_LSM6DSO32_ENABLED
           depends on ZEPHYR_HAL_ST_MODULE

If a board or shield DTS instantiates ``st,lsm6dso``, the resolver
adds ``hal_st``. The same applies to TDK, Würth, and other drivers
that already ``depends on ZEPHYR_*_MODULE``.

A Kconfig-only dependency (no ``DT_HAS_*``) is included when the
application :file:`prj.conf` enables that symbol.

Devicetree bindings
===================

A binding may also list projects explicitly:

.. code-block:: yaml

   compatible: "st,lsm6dso"
   modules:
     - hal_st

SoC metadata
============

The SoC *code* often needs a vendor HAL even for ``hello_world``.
That belongs on :file:`soc.yml` (inherited by family / series / SoC):

.. code-block:: yaml

   modules:
     - cmsis
     - cmsis_6
     - hal_stm32

   family:
     - name: stm32
       ...

Board and shield YAML
=====================

:file:`board.yml` and :file:`shield.yml` may list extras that are not
implied by DTS or Kconfig. They are an escape hatch, not the primary
place for driver HALs.

Application / sample
====================

Twister already has a ``modules:`` list on :file:`tests.yaml` /
:file:`sample.yaml`. The resolver reuses it when ``--app`` is given
(for example ``lvgl``, ``tflite-micro``, ``nanopb``).

Resolution
**********

The project list is the union of:

* :zephyr_file:`scripts/modules-defaults.yml` (currently ``picolibc``)
* ``modules:`` on every SoC used by the board
* ``modules:`` on the board and requested shields
* Driver/subsystem ``depends on ZEPHYR_*_MODULE`` whose ``DT_HAS_*``
  matches a compatible in the board, shield, or app DTS
* Binding ``modules:`` for those same compatibles
* Application ``modules:`` from Twister YAML
* Kconfig-only ``ZEPHYR_*_MODULE`` deps enabled in :file:`prj.conf`

CMake check
***********

CMake compares the resolved list with discovered modules. Missing
projects produce a warning and a ``west modules fetch`` hint.

``-DZEPHYR_REQUIRE_MODULE_DEPS=ON`` turns that into a configuration
error. ``-DZEPHYR_SKIP_MODULE_DEPS=ON`` disables the check.

CI and headless use
*******************

Existing CI jobs that run ``west update`` do not need to change.

.. code-block:: console

   west modules fetch -b ${BOARD} --app ${APP}
   west build -b ${BOARD} ${APP} -DZEPHYR_REQUIRE_MODULE_DEPS=ON

   west config manifest.group-filter -- -hal
   west update
   west modules fetch -b nucleo_f401re --shield x_nucleo_iks01a3

Progressive migration
*********************

1. Keep ``west update`` as the default.
2. SoC HALs in :file:`soc.yml` (started for vendors with a matching
   west project).
3. Driver deps are already in Kconfig; more can be added the same way.
4. Samples keep using Twister ``modules:``.
5. Disabling the ``hal`` group by default is a later, separate change.
