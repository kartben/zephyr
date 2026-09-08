# Copyright 2020 Google LLC
#
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(max17055.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP max17055_emul.c)
