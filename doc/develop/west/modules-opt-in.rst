.. _west-modules-opt-in:

Opt-in module fetching
######################

The default :file:`west.yml` manifest still downloads every active west
project when you run ``west update``. Most of those projects are vendor
HALs and other modules that a typical application never uses.

This page describes an **opt-in** workflow that fetches only the
projects a given board (and optional shields) actually need. Default
``west update`` behavior is unchanged so existing workspaces and CI
jobs stay compatible. Hardware metadata can be added incrementally.

.. _west-modules-cmd:

``west modules``
****************

``west modules`` resolves west project names declared in hardware
metadata and can fetch just those projects.

.. code-block:: console

   # Show projects required by a board
   west modules list -b nucleo_f401re

   # Include a shield that pulls in extra modules (for example a sensor HAL)
   west modules list -b nucleo_f401re --shield x_nucleo_iks01a3

   # Fetch only those projects (non-interactive; safe for CI)
   west modules fetch -b nucleo_f401re

   # Print the project list without cloning
   west modules fetch --dry-run -b qemu_x86

   # Fail if a required project is not cloned (useful in scripts)
   west modules check -b nrf52840dk/nrf52840

A fresh workspace can then look like this:

.. code-block:: console

   west init
   west modules fetch -b nucleo_f401re
   west build -b nucleo_f401re samples/hello_world

``west modules fetch`` always updates the named projects, even when
the ``hal`` group is disabled in ``manifest.group-filter``. Use
``west modules fetch --all`` to run a full ``west update``.

``west build --fetch-modules`` (or ``west config zephyr.fetch-modules true``)
runs the same resolution step before CMake.

West is optional. The resolver lives in
:zephyr_file:`scripts/list_modules.py` and is also invoked by CMake.

Declaring dependencies
**********************

Required west project names are listed in a ``modules:`` key. Child
nodes inherit parent lists.

SoC family, series, or SoC (:file:`soc.yml`):

.. code-block:: yaml

   modules:
     - cmsis
     - cmsis_6
     - hal_stm32

   family:
     - name: stm32
       series:
         - name: stm32f4x
           socs:
             - name: stm32f401xe

Board extras such as on-board sensor HALs (:file:`board.yml`):

.. code-block:: yaml

   board:
     name: sensortile_box
     full_name: SensorTile.box
     vendor: st
     socs:
       - name: stm32l4r9xx
     modules:
       - hal_st

Shield extras (:file:`shield.yml`):

.. code-block:: yaml

   shield:
     name: x_nucleo_iks01a3
     full_name: X-NUCLEO-IKS01A3
     vendor: st
     modules:
       - hal_st

Names must match the west project ``name:`` in :file:`west.yml`.
Resolution is the union of:

* :zephyr_file:`scripts/modules-defaults.yml` (currently ``picolibc``)
* every SoC used by the board
* the board itself
* each requested shield

Kconfig ``depends on ZEPHYR_<NAME>_MODULE`` (used by many sensor
drivers) remains the build-time gate: a missing module simply disables
that driver. Hardware YAML is the fetch-time declaration so the
module can be cloned **before** Kconfig runs.

Projects that are not HALs (LVGL, OpenThread, hostap, ...) are not
fetched by ``west modules fetch -b ...`` until something declares
them. Fetch those individually with ``west update <project>`` when a
sample needs them.

CMake check
***********

After the board and shields are known, CMake compares the resolved
project list with the modules already discovered. Missing projects
produce a warning and a ``west modules fetch`` hint.

Set ``-DZEPHYR_REQUIRE_MODULE_DEPS=ON`` to turn that warning into a
configuration error (recommended for opt-in and headless CI). Set
``-DZEPHYR_SKIP_MODULE_DEPS=ON`` to disable the check.

CI and headless use
*******************

Existing CI jobs that run ``west update`` do not need to change.

To adopt the opt-in path in a job that only builds one board:

.. code-block:: console

   west modules fetch -b ${BOARD}
   west build -b ${BOARD} -DZEPHYR_REQUIRE_MODULE_DEPS=ON ...

To preview the project list for a test plan:

.. code-block:: console

   west modules fetch --dry-run -b nucleo_f401re --shield x_nucleo_iks01a3

Users who already disable the ``hal`` group can keep doing so:

.. code-block:: console

   west config manifest.group-filter -- -hal
   west update
   west modules fetch -b nucleo_f401re

Progressive migration
*********************

1. Keep ``west update`` as the default so nothing breaks.
2. Declare SoC HALs in :file:`soc.yml` (started in this tree for
   vendors that have a matching west project).
3. Declare board- and shield-level extras (sensor HALs, optional
   libraries) as they are identified.
4. Later, the default ``group-filter`` can disable ``hal`` once
   metadata coverage is good enough. That flip is a separate decision.

Out-of-tree boards and SoCs use the same ``modules:`` key in their
own :file:`board.yml` / :file:`soc.yml` and pass ``--board-root`` /
``--soc-root`` (or ``BOARD_ROOT`` / ``SOC_ROOT``) as they already do.
