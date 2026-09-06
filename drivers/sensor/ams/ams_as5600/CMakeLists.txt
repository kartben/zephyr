# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(ams_as5600.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP ams_as5600_emul.c)
