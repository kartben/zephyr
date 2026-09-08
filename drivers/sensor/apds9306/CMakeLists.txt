# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2024 Daniel Kampert
# Author: Daniel Kampert <DanielKampert@Kampis-Elektroecke.de>

zephyr_library()
zephyr_library_sources(apds9306.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP apds9306_emul.c)
