# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lvgl)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})

set(llext_long_label_src_file ${PROJECT_SOURCE_DIR}/src/llexts/long_label.c)
set(llext_long_label_bin_file ${ZEPHYR_BINARY_DIR}/long_label.llext)
set(llext_long_label_inc_file ${ZEPHYR_BINARY_DIR}/long_label.inc)

set(llext_image_src_file ${PROJECT_SOURCE_DIR}/src/llexts/image.c)
set(llext_image_bin_file ${ZEPHYR_BINARY_DIR}/image.llext)
set(llext_image_inc_file ${ZEPHYR_BINARY_DIR}/image.inc)

set(llext_progress_bar_src_file ${PROJECT_SOURCE_DIR}/src/llexts/progress_bar.c)
set(llext_progress_bar_bin_file ${ZEPHYR_BINARY_DIR}/progress_bar.llext)
set(llext_progress_bar_inc_file ${ZEPHYR_BINARY_DIR}/progress_bar.inc)

add_llext_target(llext_long_label
  OUTPUT  ${llext_long_label_bin_file}
  SOURCES ${llext_long_label_src_file}
)

add_llext_target(llext_image
  OUTPUT  ${llext_image_bin_file}
  SOURCES ${llext_image_src_file}
)

add_llext_target(llext_progress_bar
  OUTPUT  ${llext_progress_bar_bin_file}
  SOURCES ${llext_progress_bar_src_file}
)

generate_inc_file_for_target(app ${llext_long_label_bin_file} ${llext_long_label_inc_file})
generate_inc_file_for_target(app ${llext_image_bin_file} ${llext_image_inc_file})
generate_inc_file_for_target(app ${llext_progress_bar_bin_file} ${llext_progress_bar_inc_file})
