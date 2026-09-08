# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(android_auto_headunit)

list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
include(nanopb)

zephyr_nanopb_sources(app src/aa.proto)

target_sources(app PRIVATE
  src/aa_control.c
  src/aa_frame.c
  src/aa_input.c
  src/aa_session.c
  src/aa_tls.c
  src/aa_video.c
  src/h264_ipcm_decode.c
  src/main.c
)

target_sources_ifdef(CONFIG_SAMPLE_AA_HU_TRANSPORT_TCP app PRIVATE src/aa_transport_tcp.c)

if(CONFIG_SAMPLE_AA_HU_TRANSPORT_USB_HOST)
  target_sources(app PRIVATE src/aa_transport_usbh.c)
  # The host stack keeps its request and descriptor helpers private
  target_include_directories(app PRIVATE ${ZEPHYR_BASE}/subsys/usb/host)
endif()

if(CONFIG_SAMPLE_AA_HU_FB_DUMP)
  target_sources(app PRIVATE src/hu_fb_dump.c)
  target_sources(native_simulator INTERFACE src/hu_fb_dump_bottom.c)
endif()

# Embed the PEM credentials, NUL terminated by the including source file
set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated)
foreach(cred cert key)
  string(TOUPPER ${cred} cred_upper)
  set(cred_path "${CONFIG_SAMPLE_AA_HU_${cred_upper}_FILE}")
  if(NOT IS_ABSOLUTE "${cred_path}")
    set(cred_path "${CMAKE_CURRENT_SOURCE_DIR}/${cred_path}")
  endif()
  generate_inc_file_for_target(app ${cred_path} ${gen_dir}/hu_${cred}.pem.inc)
endforeach()
