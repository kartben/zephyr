# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(ist8310.c ist8310_i2c.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP ist8310_emul.c)
