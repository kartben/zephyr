# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(bmg160.c)
zephyr_library_sources_ifdef(CONFIG_BMG160_TRIGGER bmg160_trigger.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP bmg160_emul.c)
