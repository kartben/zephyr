# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Glue building ESP-DL from an ESP-DL source tree
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/modules/esp-dl)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mpipe_espdet)

target_sources(app PRIVATE
  src/main.c
  src/espdet_overlay.c
  src/overlay.c
  src/detector.cpp
)
target_link_libraries(app PRIVATE esp_dl_interface)

# The model, an .espdl file exported by ESP-Detection or taken from the ESP-DL
# model zoo, is embedded in the application.
if(NOT DEFINED ESPDET_MODEL OR NOT EXISTS ${ESPDET_MODEL})
  message(FATAL_ERROR "No model to embed: pass -DESPDET_MODEL=<path to an .espdl file>")
endif()
message(STATUS "ESPDet model: ${ESPDET_MODEL}")

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated)
generate_inc_file_for_target(app ${ESPDET_MODEL} ${gen_dir}/espdet_model.inc)
