# SPDX-License-Identifier: Apache-2.0
#
# Copyright (c) 2022, Nordic Semiconductor ASA

#[=======================================================================[.rst:
FindDeprecated
**************

Find and handle deprecated CMake build code.

This module provides a centralized location for managing deprecated CMake build code. When CMake
code is deprecated, it should be moved to this module and corresponding COMPONENTS should be created
with names identifying the deprecated code.

This makes it easier to maintain deprecated code and clean it up after it has been deprecated for
two releases.

Variables
=========

.. cmake:variable:: Deprecated_FOUND
.. cmake:variable:: DEPRECATED_FOUND

   Set to True if the Deprecated component was found and loaded.

Usage
=====

When you have deprecated code in your CMakeLists.txt, such as:

.. code-block:: cmake

   if(DEPRECATED_VAR)
     deprecated()
   endif()

Move it to this module and load it as:

.. code-block:: cmake

   find_package(Deprecated COMPONENTS <deprecated_name>)
   if(<deprecated_name> IN_LIST Deprecated_FIND_COMPONENTS)
     # This code has been deprecated after Zephyr x.y
     if(DEPRECATED_VAR)
       deprecated()
     endif()
   endif()

#]=======================================================================]

if("${Deprecated_FIND_COMPONENTS}" STREQUAL "")
  message(WARNING "find_package(Deprecated) missing required COMPONENTS keyword")
endif()

if("toolchain_ld_base" IN_LIST Deprecated_FIND_COMPONENTS)
  # This code was deprecated after Zephyr v4.0.0
  list(REMOVE_ITEM Deprecated_FIND_COMPONENTS toolchain_ld_base)

  if(COMMAND toolchain_ld_base)
    message(DEPRECATION
      "The macro/function 'toolchain_ld_base' is deprecated. "
      "Please use '${LINKER}/linker_flags.cmake' and define the appropriate "
      "linker flags as properties instead. "
      "See '${ZEPHYR_BASE}/cmake/linker/linker_flags_template.cmake' for "
      "known linker properties."
    )
    toolchain_ld_base()
  endif()
endif()

if("toolchain_ld_baremetal" IN_LIST Deprecated_FIND_COMPONENTS)
  # This code was deprecated after Zephyr v4.0.0
  list(REMOVE_ITEM Deprecated_FIND_COMPONENTS toolchain_ld_baremetal)

  if(COMMAND toolchain_ld_baremetal)
    message(DEPRECATION
      "The macro/function 'toolchain_ld_baremetal' is deprecated. "
      "Please use '${LINKER}/linker_flags.cmake' and define the appropriate "
      "linker flags as properties instead. "
      "See '${ZEPHYR_BASE}/cmake/linker/linker_flags_template.cmake' for "
      "known linker properties."
    )
    toolchain_ld_baremetal()
  endif()
endif()

if("toolchain_ld_cpp" IN_LIST Deprecated_FIND_COMPONENTS)
  # This code was deprecated after Zephyr v4.0.0
  list(REMOVE_ITEM Deprecated_FIND_COMPONENTS toolchain_ld_cpp)

  if(COMMAND toolchain_ld_cpp)
    message(DEPRECATION
      "The macro/function 'toolchain_ld_cpp' is deprecated. "
      "Please use '${LINKER}/linker_flags.cmake' and define the appropriate "
      "linker flags as properties instead. "
      "See '${ZEPHYR_BASE}/cmake/linker/linker_flags_template.cmake' for "
      "known linker properties."
    )
    toolchain_ld_cpp()
  endif()
endif()

if(NOT "${Deprecated_FIND_COMPONENTS}" STREQUAL "")
  message(STATUS "The following deprecated component(s) could not be found: "
    "${Deprecated_FIND_COMPONENTS}")
endif()

set(Deprecated_FOUND True)
set(DEPRECATED_FOUND True)
