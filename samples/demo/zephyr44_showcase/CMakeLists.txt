# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Zephyr Project Contributors

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(securevault_node)

target_sources(app PRIVATE
  src/main.c
  src/auth.c
  src/monitor.c
  src/shell.c
)

# WireGuard reporting is optional — requires the zephyr-wireguard module.
# Build without it by passing -DCONFIG_WIREGUARD=n on the command line.
if(CONFIG_WIREGUARD)
  target_sources(app PRIVATE src/report.c)
  zephyr_library_include_directories(
    ${ZEPHYR_WIREGUARD_MODULE_DIR}/include
  )
endif()
