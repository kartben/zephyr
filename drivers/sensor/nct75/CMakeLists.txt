# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_NCT75 nct75.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP nct75_emul.c)
