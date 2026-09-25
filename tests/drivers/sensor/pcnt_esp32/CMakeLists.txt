# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pcnt_esp32)

# The driver needs the Espressif HAL: only its decoder is built
set(PCNT_ESP32_DIR ${ZEPHYR_BASE}/drivers/sensor/espressif/pcnt_esp32)
target_include_directories(app PRIVATE ${PCNT_ESP32_DIR})
target_sources(app PRIVATE src/main.c ${PCNT_ESP32_DIR}/pcnt_esp32_decoder.c)
