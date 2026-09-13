# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpp)

FILE(GLOB app_sources src/*.cpp)
target_sources(app PRIVATE ${app_sources})

# Compiled as C, to be compared against the C++ view of the same headers.
target_sources(app PRIVATE src/layout_c.c)
