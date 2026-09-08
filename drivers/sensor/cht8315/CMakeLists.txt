# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(cht8315.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP cht8315_emul.c)
