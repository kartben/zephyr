# Copyright (c) 2023 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

set(SAMPLE_USBD_DIR ${ZEPHYR_BASE}/samples/subsys/usb/common)

target_include_directories(app PRIVATE ${SAMPLE_USBD_DIR})
target_sources_ifdef(CONFIG_USB_DEVICE_STACK_NEXT app PRIVATE
  ${SAMPLE_USBD_DIR}/sample_usbd_init.c
)

# The samples keep a file local pointer named after the 'sample_usbd' device
# defined here, so this file must not share a translation unit with them in a
# unity build.
set_property(SOURCE ${SAMPLE_USBD_DIR}/sample_usbd_init.c
             DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
             PROPERTY SKIP_UNITY_BUILD_INCLUSION TRUE
)
