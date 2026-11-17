# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rfid_nt3h2x11)

target_include_directories(app PRIVATE ${ZEPHYR_BASE}/drivers/rfid)
target_sources(app PRIVATE src/main.c)
