# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(
  spa06.c
  spa06_bus.c
)
zephyr_library_sources_ifdef(CONFIG_SPA06_TRIGGER spa06_trigger.c)
zephyr_library_sources_ifdef(CONFIG_SENSOR_ASYNC_API
  spa06_async.c
  spa06_decoder.c
)
zephyr_library_sources_ifdef(CONFIG_SPA06_STREAM spa06_stream.c)
