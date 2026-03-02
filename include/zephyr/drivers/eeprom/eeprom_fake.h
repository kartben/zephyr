/*
 * Copyright (c) 2022 Vestas Wind Systems A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup eeprom_interface
 * @brief FFF-based helpers for faking EEPROM devices.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_EEPROM_FAKE_EEPROM_H_
#define ZEPHYR_INCLUDE_DRIVERS_EEPROM_FAKE_EEPROM_H_

#include <zephyr/drivers/eeprom.h>
#include <zephyr/fff.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Define a fake for @ref eeprom_read. */
DECLARE_FAKE_VALUE_FUNC(int, fake_eeprom_read, const struct device *, off_t, void *, size_t);

/** @brief Define a fake for @ref eeprom_write. */
DECLARE_FAKE_VALUE_FUNC(int, fake_eeprom_write, const struct device *, off_t, const void *, size_t);

/** @brief Define a fake for @ref eeprom_get_size. */
DECLARE_FAKE_VALUE_FUNC(size_t, fake_eeprom_size, const struct device *);

/**
 * @brief Delegate `fake_eeprom_size()` to a helper.
 *
 * @param dev EEPROM device instance.
 *
 * @return EEPROM size in bytes.
 */
size_t fake_eeprom_size_delegate(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_EEPROM_FAKE_EEPROM_H_ */
