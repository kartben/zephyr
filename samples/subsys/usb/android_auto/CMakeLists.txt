# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(android_auto)

include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)

list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
include(nanopb)

zephyr_nanopb_sources(app src/aa.proto)

target_sources(app PRIVATE
  src/aa_control.c
  src/aa_display.c
  src/aa_encoder.c
  src/aa_frame.c
  src/aa_input.c
  src/aa_session.c
  src/aa_tls.c
  src/aa_touch.c
  src/aa_video.c
  src/h264_ipcm.c
  src/main.c
  src/ui.c
  src/yuv.c
)

target_sources_ifdef(CONFIG_SAMPLE_AA_TRANSPORT_USB app PRIVATE
  src/aa_aoa.c
  src/aa_transport_usb.c
)
target_sources_ifdef(CONFIG_SAMPLE_AA_TRANSPORT_TCP app PRIVATE src/aa_transport_tcp.c)
target_sources_ifdef(CONFIG_SAMPLE_AA_SENSOR_CHANNEL app PRIVATE src/aa_sensor.c)

if(CONFIG_SAMPLE_AA_H264_DUMP)
  target_sources(app PRIVATE src/h264_dump.c)
  target_sources(native_simulator INTERFACE src/h264_dump_bottom.c)
endif()

# Embed the PEM credentials, NUL terminated by the including source file
set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated)
foreach(cred cert key)
  string(TOUPPER ${cred} cred_upper)
  set(cred_path "${CONFIG_SAMPLE_AA_${cred_upper}_FILE}")
  if(NOT IS_ABSOLUTE "${cred_path}")
    set(cred_path "${CMAKE_CURRENT_SOURCE_DIR}/${cred_path}")
  endif()
  generate_inc_file_for_target(app ${cred_path} ${gen_dir}/aa_${cred}.pem.inc)
endforeach()

if(CONFIG_SAMPLE_AA_DEMO_WIDGETS)
  set(LVGL_DIR ${ZEPHYR_LVGL_MODULE_DIR})
  target_include_directories(app PRIVATE ${LVGL_DIR}/demos/)
  target_sources(app PRIVATE
    ${LVGL_DIR}/demos/widgets/assets/img_clothes.c
    ${LVGL_DIR}/demos/widgets/assets/img_demo_widgets_avatar.c
    ${LVGL_DIR}/demos/widgets/assets/img_demo_widgets_needle.c
    ${LVGL_DIR}/demos/widgets/assets/img_lvgl_logo.c
    ${LVGL_DIR}/demos/widgets/lv_demo_widgets.c
    ${LVGL_DIR}/demos/widgets/lv_demo_widgets_analytics.c
    ${LVGL_DIR}/demos/widgets/lv_demo_widgets_components.c
    ${LVGL_DIR}/demos/widgets/lv_demo_widgets_profile.c
    ${LVGL_DIR}/demos/widgets/lv_demo_widgets_shop.c
  )
endif()
