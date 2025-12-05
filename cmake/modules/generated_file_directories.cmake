# SPDX-License-Identifier: Apache-2.0

#[=======================================================================[.rst:
generated_file_directories
---------------------------

Create standard directories for generated files in the build directory.

This module creates the directory structure used by the Zephyr build system
for placing generated header files and other generated content.

Outcome
^^^^^^^

The following variables are set when this module completes:

.. cmake:variable:: BINARY_DIR_INCLUDE

   Set to ``${PROJECT_BINARY_DIR}/include``.

.. cmake:variable:: BINARY_DIR_INCLUDE_GENERATED

   Set to ``${BINARY_DIR_INCLUDE}/generated/zephyr``. This directory is
   created if it does not exist.

#]=======================================================================]

include_guard(GLOBAL)

set(BINARY_DIR_INCLUDE ${PROJECT_BINARY_DIR}/include)
set(BINARY_DIR_INCLUDE_GENERATED ${BINARY_DIR_INCLUDE}/generated/zephyr)
file(MAKE_DIRECTORY ${BINARY_DIR_INCLUDE_GENERATED})
