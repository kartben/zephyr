# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iis3dwb)

target_include_directories(app PRIVATE
  ${ZEPHYR_BASE}/drivers/sensor/st/iis3dwb
  ${ZEPHYR_BASE}/drivers/sensor/st/stmemsc
)
target_sources(app PRIVATE src/main.c)
