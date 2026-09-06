# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(si7055.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP si7055_emul.c)
