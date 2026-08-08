# Copyright (c) 2026 zenbedded contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Locate the zenbedded module (this repository) without requiring it to be in
# the west manifest, so the app builds both standalone and inside a Zephyr fork.
get_filename_component(ZENBEDDED_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES "${ZENBEDDED_ROOT}")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zenbedded_app LANGUAGES C)

target_sources(app PRIVATE src/main.c)
