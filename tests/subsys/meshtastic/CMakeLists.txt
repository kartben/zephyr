# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshtastic_tests)

target_sources(app PRIVATE
  src/main.c
  src/test_channels.c
  src/header_core_only.c
  src/header_gnss_only.c
  src/header_nodedb_only.c
  src/header_nodeinfo_only.c
  src/header_telemetry_only.c
)
target_include_directories(app PRIVATE
  ${ZEPHYR_BASE}/subsys/meshtastic
  ${CMAKE_BINARY_DIR}/zephyr/subsys/meshtastic/proto
)
