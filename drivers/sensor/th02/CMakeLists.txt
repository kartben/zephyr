# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(th02.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP th02_emul.c)
