# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(lsm6ds0.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP lsm6ds0_emul.c)
