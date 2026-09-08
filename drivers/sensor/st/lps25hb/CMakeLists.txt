# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(lps25hb.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP lps25hb_emul.c)
