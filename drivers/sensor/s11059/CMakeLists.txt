# Copyright (c) 2022 Hiroki Tada
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(s11059.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP s11059_emul.c)
