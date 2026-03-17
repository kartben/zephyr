# SPDX-License-Identifier: Apache-2.0

add_library(FREERTOS_COMPAT INTERFACE)

target_include_directories(FREERTOS_COMPAT INTERFACE
  ${PROJECT_SOURCE_DIR}/include/zephyr/portability/freertos
)

zephyr_library()
zephyr_library_sources_ifdef(
  CONFIG_FREERTOS_COMPAT
  kernel.c
  task.c
  semaphore.c
  queue.c
  timer.c
)

zephyr_library_include_directories(
  ${ZEPHYR_BASE}/kernel/include
  ${ZEPHYR_BASE}/arch/${ARCH}/include
)

zephyr_library_link_libraries(FREERTOS_COMPAT)
