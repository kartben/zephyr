# Copyright (c) 2025 Andreas Klinger
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(veml6031.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP veml6031_emul.c)
