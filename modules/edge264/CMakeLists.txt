# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_EDGE264)
  if(NOT EDGE264_DIR)
    if(ZEPHYR_EDGE264_MODULE_DIR)
      set(EDGE264_DIR ${ZEPHYR_EDGE264_MODULE_DIR})
    elseif(EXISTS ${ZEPHYR_BASE}/../modules/lib/edge264/edge264.h)
      set(EDGE264_DIR ${ZEPHYR_BASE}/../modules/lib/edge264)
    else()
      message(FATAL_ERROR
        "edge264 sources not found. Add the module in west.yml and run west update."
      )
    endif()
  endif()

  zephyr_library_named(edge264)

  zephyr_include_directories(${EDGE264_DIR})

  zephyr_library_include_directories(
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/posix_stubs
    ${EDGE264_DIR}
  )

  # GCC 13 ICEs at -O2/-O3 in inter prediction with the portable backend.
  set(edge264_cflags
    -O1
    -ffast-math
    -Wno-unused-function
  )

  if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    list(APPEND edge264_cflags
      -Wno-maybe-uninitialized
      -Wno-return-type
      -flax-vector-conversions
    )
  endif()

  zephyr_library_compile_options(${edge264_cflags})

  # GCC has no NEON/SSE-style Clang vector builtins; use the portable backend
  # even on hosts that also define __SSE2__ (native_sim).
  if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    zephyr_library_compile_definitions(SIMD=4)
  elseif(CONFIG_CPU_CORTEX_M)
    zephyr_library_compile_definitions(SIMD=4)
  endif()

  zephyr_library_sources(
    ${CMAKE_CURRENT_LIST_DIR}/edge264_zephyr.c
    ${EDGE264_DIR}/src/edge264.c
  )

  # Remap libc allocators and GCC vector builtins only for the decoder TU.
  set_source_files_properties(${EDGE264_DIR}/src/edge264.c PROPERTIES
    COMPILE_OPTIONS "-include;${CMAKE_CURRENT_LIST_DIR}/edge264_zephyr.h"
  )
endif()
