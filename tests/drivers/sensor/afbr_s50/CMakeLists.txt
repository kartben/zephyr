# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(afbr_s50)

# The driver needs the Argus library, which is only available for Cortex-M:
# build its decoder alone, with the Argus API headers of the hal_afbr module.
set(AFBR_S50_DIR ${ZEPHYR_BASE}/drivers/sensor/broadcom/afbr_s50)

target_include_directories(app PRIVATE
  ${AFBR_S50_DIR}
  ${ZEPHYR_HAL_AFBR_MODULE_DIR}/AFBR-S50/Include
)
target_sources(app PRIVATE
  src/main.c
  ${AFBR_S50_DIR}/afbr_s50_decoder.c
)
