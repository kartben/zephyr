# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(si7210.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP si7210_emul.c)
