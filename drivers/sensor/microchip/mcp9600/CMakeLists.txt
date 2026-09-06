# Copyright (c) 2022, Maxmillion McLaughlin
# Copyright (c) 2020, SER Consulting LLC / Steven Riedl
#
# SPDX-License-Identifier: Apache-2.0
#

zephyr_library()
zephyr_library_sources(mcp9600.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP mcp9600_emul.c)
