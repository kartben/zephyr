# Copyright (c) 2026 zenbedded contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

get_filename_component(ZENBEDDED_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES "${ZENBEDDED_ROOT}")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zenbedded_control_test LANGUAGES C)

target_sources(app PRIVATE
  src/test_common.c
  src/test_joint_table.c
  src/test_setpoint.c
  src/test_state_machine.c
  src/test_park.c
  src/test_resume.c
  src/test_hist.c
)
