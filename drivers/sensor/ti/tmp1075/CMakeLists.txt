# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(tmp1075.c tmp1075_trigger.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP tmp1075_emul.c)
