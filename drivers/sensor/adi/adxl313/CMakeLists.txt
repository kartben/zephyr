# Copyright (c) 2026 RAKwireless Technology Limited
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(adxl313.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP adxl313_emul.c)
