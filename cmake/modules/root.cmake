# SPDX-License-Identifier: Apache-2.0
#
# Copyright (c) 2021-2023, Nordic Semiconductor ASA

#[=======================================================================[.rst:
root
----

Convert Zephyr root paths to absolute paths.

This module converts all relative paths in existing ROOT lists to absolute
paths relative to ``APPLICATION_SOURCE_DIR``. This ensures consistent path
handling across the build system.

Optional Variables
^^^^^^^^^^^^^^^^^^

.. cmake:variable:: ARCH_ROOT

   CMake list of arch roots containing architecture implementations.

.. cmake:variable:: SOC_ROOT

   CMake list of SoC roots containing SoC implementations.

.. cmake:variable:: BOARD_ROOT

   CMake list of board roots containing board and shield implementations.

.. cmake:variable:: MODULE_EXT_ROOT

   CMake list of module external roots containing module glue code.

.. cmake:variable:: SCA_ROOT

   CMake list of SCA roots containing static code analysis integration.

.. cmake:variable:: DTS_ROOT

   CMake list of DTS roots containing devicetree bindings or includes.

Behavior
^^^^^^^^

For each defined root variable:

- Relative paths are converted to absolute paths (relative to ``APPLICATION_SOURCE_DIR``)
- The root list is updated with absolute paths
- Undefined roots remain undefined

#]=======================================================================]

include_guard(GLOBAL)

include(extensions)

# Merge in variables from other sources (e.g. sysbuild)
zephyr_get(MODULE_EXT_ROOT MERGE SYSBUILD GLOBAL)
zephyr_get(BOARD_ROOT MERGE SYSBUILD GLOBAL)
zephyr_get(SOC_ROOT MERGE SYSBUILD GLOBAL)
zephyr_get(ARCH_ROOT MERGE SYSBUILD GLOBAL)
zephyr_get(SCA_ROOT MERGE SYSBUILD GLOBAL)
zephyr_get(SNIPPET_ROOT MERGE SYSBUILD GLOBAL)
zephyr_get(DTS_ROOT MERGE SYSBUILD GLOBAL)

# Convert paths to absolute, relative from APPLICATION_SOURCE_DIR
zephyr_file(APPLICATION_ROOT MODULE_EXT_ROOT)
zephyr_file(APPLICATION_ROOT BOARD_ROOT)
zephyr_file(APPLICATION_ROOT SOC_ROOT)
zephyr_file(APPLICATION_ROOT ARCH_ROOT)
zephyr_file(APPLICATION_ROOT SCA_ROOT)
zephyr_file(APPLICATION_ROOT SNIPPET_ROOT)
zephyr_file(APPLICATION_ROOT DTS_ROOT)

if(unittest IN_LIST Zephyr_FIND_COMPONENTS)
  # Zephyr used in unittest mode, use dedicated unittest root.
  set(BOARD_ROOT ${ZEPHYR_BASE}/subsys/testsuite)
  set(ARCH_ROOT  ${ZEPHYR_BASE}/subsys/testsuite)
  set(SOC_ROOT   ${ZEPHYR_BASE}/subsys/testsuite)
endif()
