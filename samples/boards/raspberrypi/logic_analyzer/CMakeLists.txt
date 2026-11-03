# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(logic_analyzer)

target_sources(app PRIVATE src/main.c)

# hardware/dma.h is private to the Pico SDK DMA driver. The Zephyr DMA
# driver does not support a ring, so this sample programs the block itself
# and only needs the header.
target_include_directories(app PRIVATE
  ${ZEPHYR_HAL_RPI_PICO_MODULE_DIR}/src/rp2_common/hardware_dma/include
)
