# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(android_auto_headunit)

list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
include(nanopb)

zephyr_nanopb_sources(app src/aa.proto)

if(CONFIG_SOC_SERIES_STM32N6X)
  zephyr_linker_sources(SECTIONS axisram1_check.ld)
endif()

# The Espressif linker scripts place the section in PSRAM themselves
if(CONFIG_MBEDTLS_HEAP_CUSTOM_SECTION AND NOT CONFIG_ESP_MBEDTLS_HEAP_ALLOC_EXT_RAM)
  zephyr_linker_sources(SECTIONS mbedtls_heap.ld)
endif()

target_sources(app PRIVATE
  src/aa_audio.c
  src/aa_control.c
  src/aa_frame.c
  src/aa_input.c
  src/aa_layout.c
  src/aa_mic.c
  src/aa_nav.c
  src/aa_play.c
  src/aa_scale.c
  src/aa_screen.c
  src/aa_sensor.c
  src/aa_session.c
  src/aa_tls.c
  src/aa_tof.c
  src/aa_video.c
  src/h264_ipcm_decode.c
  src/main.c
)

target_sources_ifdef(CONFIG_SAMPLE_AA_HU_TRANSPORT_TCP app PRIVATE src/aa_transport_tcp.c)
target_sources_ifdef(CONFIG_SAMPLE_AA_HU_TRANSPORT_NONE app PRIVATE src/aa_transport_none.c)
target_sources_ifdef(CONFIG_SAMPLE_AA_HU_DEMO_CLIP app PRIVATE src/aa_demo.c)
target_sources_ifdef(CONFIG_SAMPLE_AA_HU_SPLIT_SCREEN app PRIVATE src/aa_gui.c)

# The decoder is the h264bsd module, which the west manifest carries; only
# the glue that drives it belongs to the sample.
target_sources_ifdef(CONFIG_SAMPLE_AA_HU_H264 app PRIVATE src/aa_h264.c)

# ESP32-P4 PIE kernels, assembled with the compiler given to h264bsd for its own
if(H264BSD_PIE_CC)
  set(pie_obj ${CMAKE_CURRENT_BINARY_DIR}/aa_pack_pie.obj)
  add_custom_command(OUTPUT ${pie_obj}
    COMMAND ${H264BSD_PIE_CC} -march=rv32imafc_zicsr_zifencei_xesploop_xespv -mabi=ilp32f
            -c ${CMAKE_CURRENT_SOURCE_DIR}/src/aa_pack_pie.S -o ${pie_obj}.tmp
    COMMAND ${CMAKE_OBJCOPY} --remove-section .riscv.attributes ${pie_obj}.tmp ${pie_obj}
    DEPENDS src/aa_pack_pie.S)
  set_source_files_properties(${pie_obj} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
  target_sources(app PRIVATE ${pie_obj})
  target_compile_definitions(app PRIVATE AA_HU_HAS_PIE)
endif()

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
