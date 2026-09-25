# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(pcnt_esp32.c)
zephyr_library_sources_ifdef(CONFIG_SENSOR_ASYNC_API pcnt_esp32_decoder.c)
