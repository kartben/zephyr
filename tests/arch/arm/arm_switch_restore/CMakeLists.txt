# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(arm_switch_restore)

target_sources(app PRIVATE
  src/main.c
  src/inject.c
)
zephyr_link_libraries("-Wl,--wrap=arm_m_switch_restore")
