# Copyright (c) 2025 Andreas Klinger
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(veml6046.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP veml6046_emul.c)
