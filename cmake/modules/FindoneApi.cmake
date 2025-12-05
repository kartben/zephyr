# SPDX-License-Identifier: Apache-2.0
#
# Copyright (c) 2023 Intel Corporation

#[=======================================================================[.rst:
FindoneApi
----------

Find the Intel oneAPI compiler toolchain.

This module locates the Intel oneAPI compiler (icx) and determines its version.

Variables
^^^^^^^^^

.. cmake:variable:: oneApi_FOUND
.. cmake:variable:: ONEAPI_FOUND

   Set to True if the oneAPI toolchain/compiler was found.

.. cmake:variable:: ONEAPI_VERSION

   The version of the oneAPI toolchain.

#]=======================================================================]

include(FindPackageHandleStandardArgs)

if(CMAKE_C_COMPILER)
  # Parse the 'clang --version' output to find the installed version.
  execute_process(COMMAND ${CMAKE_C_COMPILER} --version OUTPUT_VARIABLE ONEAPI_VERSION)
  string(REGEX REPLACE "[^0-9]*([0-9.]+) .*" "\\1" ONEAPI_VERSION ${ONEAPI_VERSION})
endif()

find_package_handle_standard_args(oneApi
  REQUIRED_VARS CMAKE_C_COMPILER
  VERSION_VAR ONEAPI_VERSION
)
