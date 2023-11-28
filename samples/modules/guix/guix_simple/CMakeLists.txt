# Copyright (c) 2023 Fabian Blatz <fabianblatz@gmail.com>
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(guix_simple)

set(GUIX_DIR ${ZEPHYR_GUIX_MODULE_DIR})

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
