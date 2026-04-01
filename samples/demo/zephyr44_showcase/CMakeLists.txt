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
  src/report.c
)

# WireGuard stack is optional (zephyr-wireguard west module). report.c is always
# built; the full VPN path is compiled only when CONFIG_WIREGUARD=y (see report.c).
# Build without WireGuard: west build ... -- -DCONFIG_WIREGUARD=n
if(CONFIG_WIREGUARD)
  zephyr_library_include_directories(
    ${ZEPHYR_WIREGUARD_MODULE_DIR}/include
  )
endif()
