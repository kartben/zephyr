# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(ak8975.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP ak8975_emul.c)
