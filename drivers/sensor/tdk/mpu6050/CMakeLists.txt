# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(mpu6050.c)
zephyr_library_sources_ifdef(CONFIG_MPU6050_TRIGGER mpu6050_trigger.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_SENSOR_REGMAP mpu6050_emul.c)
