# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(max44009.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP max44009_emul.c)
