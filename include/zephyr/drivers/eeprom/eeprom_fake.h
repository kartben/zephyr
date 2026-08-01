/*
 * Copyright (c) 2022 Vestas Wind Systems A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Fake EEPROM driver API functions.
 * @ingroup eeprom_fake
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_EEPROM_FAKE_EEPROM_H_
#define ZEPHYR_INCLUDE_DRIVERS_EEPROM_FAKE_EEPROM_H_

#include <zephyr/drivers/eeprom.h>
#include <zephyr/fff.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fake EEPROM driver API functions.
 * @defgroup eeprom_fake Fake EEPROM
 * @ingroup io_emulators
 * @ingroup eeprom_interface
 * @{
 */

/**
 * @brief Read data from the fake EEPROM.
 *
 * @see eeprom_read
 */
DECLARE_FAKE_VALUE_FUNC(int, fake_eeprom_read, const struct device *, off_t, void *, size_t);

/**
 * @brief Write data to the fake EEPROM.
 *
 * @see eeprom_write
 */
DECLARE_FAKE_VALUE_FUNC(int, fake_eeprom_write, const struct device *, off_t, const void *, size_t);

/**
 * @brief Get the size of the fake EEPROM in bytes.
 *
 * @see eeprom_get_size
 */
DECLARE_FAKE_VALUE_FUNC(size_t, fake_eeprom_size, const struct device *);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_EEPROM_FAKE_EEPROM_H_ */
