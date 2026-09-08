# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(si7060.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP si7060_emul.c)
