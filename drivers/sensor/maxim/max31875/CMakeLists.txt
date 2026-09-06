# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(max31875.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP max31875_emul.c)
