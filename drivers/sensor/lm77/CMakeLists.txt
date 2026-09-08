# Copyright (c) 2021 Vestas Wind Systems A/S
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(lm77.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP lm77_emul.c)
