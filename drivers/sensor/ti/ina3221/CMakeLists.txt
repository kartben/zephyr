# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(ina3221.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP ina3221_emul.c)
