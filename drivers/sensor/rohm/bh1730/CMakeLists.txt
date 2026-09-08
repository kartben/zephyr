# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(bh1730.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP bh1730_emul.c)
