# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(qmi8658a.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP qmi8658a_emul.c)
