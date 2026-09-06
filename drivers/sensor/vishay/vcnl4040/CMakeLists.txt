# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(vcnl4040.c)
zephyr_library_sources_ifdef(CONFIG_VCNL4040_TRIGGER vcnl4040_trigger.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP vcnl4040_emul.c)
