# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(apds9253.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP apds9253_emul.c)
