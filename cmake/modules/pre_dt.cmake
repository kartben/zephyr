# SPDX-License-Identifier: Apache-2.0
#
# Copyright (c) 2021, 2023 Nordic Semiconductor ASA

#[=======================================================================[.rst:
pre_dt
------

Finalize devicetree root paths before devicetree processing.

This module finalizes the ``DTS_ROOT`` variable so the build system knows
where to find all devicetree files, bindings, and vendor prefixes. It must
be included before the main :cmake:module:`dts` module.

Outcome
^^^^^^^

The following variables are set when this module completes:

.. cmake:variable:: DTS_ROOT

   Deduplicated list of devicetree implementation file locations (bindings,
   vendor prefixes, etc.).

.. cmake:variable:: DTS_ROOT_SYSTEM_INCLUDE_DIRS

   Space-separated paths for C preprocessor #includes in devicetree files.

Optional Variables
^^^^^^^^^^^^^^^^^^

.. cmake:variable:: APPLICATION_SOURCE_DIR

   Application path (added to ``DTS_ROOT``).

.. cmake:variable:: BOARD_DIR

   Board definition directory (added to ``DTS_ROOT``).

.. cmake:variable:: DTS_ROOT

   Initial ``DTS_ROOT`` contents may be pre-populated.

.. cmake:variable:: ZEPHYR_BASE

   Zephyr repository path (added to ``DTS_ROOT``).

.. cmake:variable:: SHIELD_DIRS

   Shield definition paths (added to ``DTS_ROOT``).

#]=======================================================================]

include_guard(GLOBAL)
include(extensions)

# Using a function avoids polluting the parent scope unnecessarily.
function(pre_dt_module_run)
  # Convert relative paths to absolute paths relative to the application
  # source directory.
  zephyr_file(APPLICATION_ROOT DTS_ROOT)

  # DTS_ROOT always includes the application directory, the board
  # directory, shield directories, and ZEPHYR_BASE.
  list(APPEND
    DTS_ROOT
    ${APPLICATION_SOURCE_DIR}
    ${BOARD_DIR}
    ${SHIELD_DIRS}
    ${ZEPHYR_BASE}
    )

  # Convert the directories in DTS_ROOT to absolute paths without
  # symlinks.
  #
  # DTS directories can come from multiple places. Some places, like a
  # user's CMakeLists.txt can preserve symbolic links. Others, like
  # scripts/zephyr_module.py --settings-out resolve them.
  unset(real_dts_root)
  foreach(dts_dir ${DTS_ROOT})
    file(REAL_PATH ${dts_dir} real_dts_dir)
    list(APPEND real_dts_root ${real_dts_dir})
  endforeach()
  set(DTS_ROOT ${real_dts_root})

  # Finalize DTS_ROOT.
  list(REMOVE_DUPLICATES DTS_ROOT)

  foreach(arch ${ARCH_V2_NAME_LIST})
    list(APPEND arch_include dts/${arch})
  endforeach()

  # Finalize DTS_ROOT_SYSTEM_INCLUDE_DIRS.
  set(DTS_ROOT_SYSTEM_INCLUDE_DIRS)
  foreach(dts_root ${DTS_ROOT})
    foreach(dts_root_path
        include
        include/zephyr
        dts/common
        dts/vendor
        ${arch_include}
        dts
        )
      get_filename_component(full_path ${dts_root}/${dts_root_path} REALPATH)
      if(EXISTS ${full_path})
        list(APPEND DTS_ROOT_SYSTEM_INCLUDE_DIRS ${full_path})
      endif()
    endforeach()
  endforeach()

  # Set output variables.
  set(DTS_ROOT ${DTS_ROOT} PARENT_SCOPE)
  set(DTS_ROOT_SYSTEM_INCLUDE_DIRS ${DTS_ROOT_SYSTEM_INCLUDE_DIRS} PARENT_SCOPE)
endfunction()

pre_dt_module_run()
