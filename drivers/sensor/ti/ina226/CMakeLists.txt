# Copyright 2024 NXP
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_INA226 ina226.c)
zephyr_library_sources_ifdef(CONFIG_INA226_TRIGGER ina226_trigger.c)
