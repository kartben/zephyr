# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hello_world)

target_sources(app PRIVATE src/main.cpp)

include(edge-impulse-sdk/cmake/utils.cmake)

# Add the Edge Impulse SDK
add_subdirectory(edge-impulse-sdk/cmake/zephyr)

# Add model sources
target_sources(app PRIVATE
    tflite-model/tflite_learn_143_compiled.cpp
    )

# Edge impulse SDK include directories
set(INCLUDES
    .
    src
    tflite-model
    model-parameters
    )
include_directories(${INCLUDES})
