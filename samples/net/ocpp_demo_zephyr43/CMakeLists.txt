# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ocpp_demo_zephyr43)

target_sources(app PRIVATE
    src/main.c
    src/gui.c
    src/cpu_monitor.c
    src/usb_status.c
)
