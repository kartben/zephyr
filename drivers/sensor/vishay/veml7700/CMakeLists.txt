# Copyright (c) 2023 Andreas Kilian
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(veml7700.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP veml7700_emul.c)
