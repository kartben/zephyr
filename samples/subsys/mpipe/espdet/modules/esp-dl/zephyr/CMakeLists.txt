# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

if(NOT CONFIG_ESP_DL)
  return()
endif()

if(NOT DEFINED ESP_DL_DIR AND DEFINED ENV{ESP_DL_DIR})
  set(ESP_DL_DIR $ENV{ESP_DL_DIR})
endif()
set(ESP_DL_DIR "${ESP_DL_DIR}" CACHE PATH "Path to the esp-dl directory of an ESP-DL checkout")

if(NOT EXISTS ${ESP_DL_DIR}/dl/dl_define.hpp)
  message(FATAL_ERROR
    "ESP-DL not found at '${ESP_DL_DIR}'. Clone it with:\n"
    "  git clone --depth 1 --branch v3.3.11 --filter=blob:none --sparse "
    "https://github.com/espressif/esp-dl <path>\n"
    "  git -C <path> sparse-checkout set esp-dl\n"
    "and pass -DESP_DL_DIR=<path>/esp-dl"
  )
endif()

set(ESP_DL_COMPAT ${CMAKE_CURRENT_LIST_DIR}/../compat)

# Settings ESP-DL takes from the ESP-IDF sdkconfig. Everything that includes
# ESP-DL headers must see the same values, so they travel with this interface.
zephyr_interface_library_named(esp_dl_interface)
target_compile_definitions(esp_dl_interface INTERFACE
  CONFIG_IDF_TARGET_ESP32S3=1
  CONFIG_FREERTOS_NUMBER_OF_CORES=1
  # Camera frames are RGB565 and model inputs RGB888: the only conversion built
  CONFIG_PIX_CVT_RGB565_TO_RGB888_SUPPORT=1
)
target_include_directories(esp_dl_interface SYSTEM INTERFACE
  ${ESP_DL_COMPAT}/include
  ${ESP_DL_DIR}/dl
  ${ESP_DL_DIR}/dl/tool/include
  ${ESP_DL_DIR}/dl/tensor/include
  ${ESP_DL_DIR}/dl/base
  ${ESP_DL_DIR}/dl/base/isa
  ${ESP_DL_DIR}/dl/base/isa/tie728
  ${ESP_DL_DIR}/dl/base/isa/xtensa
  ${ESP_DL_DIR}/dl/math/include
  ${ESP_DL_DIR}/dl/model/include
  ${ESP_DL_DIR}/dl/module/include
  ${ESP_DL_DIR}/fbs_loader/include
  ${ESP_DL_DIR}/vision/detect
  ${ESP_DL_DIR}/vision/image
  ${ESP_DL_DIR}/vision/image/isa
)
# ESP-DL headers rely on the heap_caps API without including it, and allocate
# through it: route it to the heaps set up in esp_dl_heap.c.
target_compile_options(esp_dl_interface INTERFACE
  $<$<COMPILE_LANGUAGE:CXX>:-include${ESP_DL_COMPAT}/include/esp_dl_zephyr_heap.h>
)

zephyr_library_named(esp_dl)
zephyr_library_link_libraries(esp_dl_interface)

file(GLOB esp_dl_sources
  ${ESP_DL_DIR}/dl/tool/src/*.cpp
  ${ESP_DL_DIR}/dl/tool/isa/xtensa/*.S
  ${ESP_DL_DIR}/dl/tool/isa/tie728/*.S
  ${ESP_DL_DIR}/dl/tensor/src/*.cpp
  ${ESP_DL_DIR}/dl/base/*.cpp
  ${ESP_DL_DIR}/dl/base/isa/xtensa/*.S
  ${ESP_DL_DIR}/dl/base/isa/tie728/*.S
  ${ESP_DL_DIR}/dl/math/src/*.cpp
  ${ESP_DL_DIR}/dl/model/src/*.cpp
  ${ESP_DL_DIR}/dl/module/src/*.cpp
  ${ESP_DL_DIR}/fbs_loader/src/*.cpp
  ${ESP_DL_DIR}/vision/detect/dl_detect_base.cpp
  ${ESP_DL_DIR}/vision/detect/dl_detect_postprocessor.cpp
  ${ESP_DL_DIR}/vision/detect/dl_detect_espdet_postprocessor.cpp
  ${ESP_DL_DIR}/vision/image/dl_image_preprocessor.cpp
  ${ESP_DL_DIR}/vision/image/dl_image_process.cpp
  ${ESP_DL_DIR}/vision/image/dl_image_pixel_cvt_dispatch_rgb5652rgb888.cpp
)

zephyr_library_sources(
  ${esp_dl_sources}
  ${ESP_DL_COMPAT}/src/esp_dl_cpu.c
  ${ESP_DL_COMPAT}/src/esp_dl_file.c
  ${ESP_DL_COMPAT}/src/esp_dl_heap.c
  # ESP-DL only ships its model reader prebuilt: use an open implementation
  ${ESP_DL_COMPAT}/src/fbs_model.cpp
)

if(NOT CONFIG_PSA_CRYPTO)
  # Only encrypted models need the cipher and hash, which then fail to load.
  zephyr_library_include_directories(${ESP_DL_COMPAT}/crypto)
endif()

# The model loader uses strlcpy()
zephyr_library_compile_definitions(_DEFAULT_SOURCE)

zephyr_library_compile_options(
  -O3
  -ffast-math
  -Wno-array-bounds
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-copy>
  -Wno-strict-aliasing
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-overloaded-virtual>
  -Wno-unused-variable
  -Wno-unused-but-set-variable
  -Wno-sign-compare
  -Wno-format
  -Wno-missing-field-initializers
  -Wno-double-promotion
  -Wno-format-security
)

# ESP-DL also releases the memory of its heaps with free(): route those
# pointers back to the heaps (see esp_dl_heap.c).
zephyr_ld_options(-Wl,--wrap=free)
