# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(tmp112.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP tmp112_emul.c)
