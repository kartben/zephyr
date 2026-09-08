#
# Copyright (c) 2018 Philémon Jaermann
#
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(lsm303dlhc_magn.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP lsm303dlhc_magn_emul.c)
