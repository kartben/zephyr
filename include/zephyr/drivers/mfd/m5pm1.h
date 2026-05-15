/*
 * Copyright (c) 2026 Benjamin Cabé <benjamin@zephyrproject.org>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MFD_M5PM1_H_
#define ZEPHYR_INCLUDE_DRIVERS_MFD_M5PM1_H_

#include <stdint.h>

struct device;

/* Register map (subset; see https://github.com/m5stack/M5PM1) */
#define M5PM1_REG_DEVICE_ID    0x00
#define M5PM1_REG_DEVICE_MODEL 0x01
#define M5PM1_REG_HW_REV       0x02
#define M5PM1_REG_SW_REV       0x03
#define M5PM1_REG_PWR_CFG      0x06
#define M5PM1_REG_HOLD_CFG     0x07
#define M5PM1_REG_I2C_CFG      0x09
#define M5PM1_REG_SYS_CMD      0x0C

/* PWR_CFG bits */
#define M5PM1_PWR_CFG_CHG_EN   BIT(0)
#define M5PM1_PWR_CFG_DCDC_EN  BIT(1)
#define M5PM1_PWR_CFG_LDO_EN   BIT(2)
#define M5PM1_PWR_CFG_BOOST_EN BIT(3)

/* I2C_CFG: low nibble is sleep timeout in seconds (0 disables) */
#define M5PM1_I2C_CFG_SLEEP_MASK 0x0F

int mfd_m5pm1_read_reg(const struct device *dev, uint8_t reg, uint8_t *val);
int mfd_m5pm1_write_reg(const struct device *dev, uint8_t reg, uint8_t val);
int mfd_m5pm1_update_reg(const struct device *dev, uint8_t reg, uint8_t mask, uint8_t val);

#endif /* ZEPHYR_INCLUDE_DRIVERS_MFD_M5PM1_H_ */
