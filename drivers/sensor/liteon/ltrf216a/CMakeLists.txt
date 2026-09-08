# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(ltrf216a.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP ltrf216a_emul.c)
