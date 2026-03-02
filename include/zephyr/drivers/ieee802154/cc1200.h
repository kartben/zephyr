/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup ieee802154_driver
 * @brief CC1200 radio helper definitions.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_IEEE802154_CC1200_H_
#define ZEPHYR_INCLUDE_DRIVERS_IEEE802154_CC1200_H_

#include <zephyr/device.h>

/**
 * @brief Store CC1200 RF register settings.
 *
 * The first 42 entries in @ref registers target addresses 0x04 to 0x2D.
 * The next 58 entries target extended addresses 0x00 to 0x39.
 */
struct cc1200_rf_registers_set {
	/** Center frequency for channel 0, in Hz. */
	uint32_t chan_center_freq0;
	/** Channel spacing in 100 Hz units (for example, 12.5 kHz is 125). */
	uint16_t channel_spacing;
	/** Register values to load into the CC1200 transceiver. */
	uint8_t registers[100];
};

#ifndef CONFIG_IEEE802154_CC1200_RF_PRESET
/**
 * @brief Provide RF settings when @kconfig{CONFIG_IEEE802154_CC1200_RF_PRESET} is disabled.
 *
 * These values can be generated with TI SmartRF Studio.
 */
extern const struct cc1200_rf_registers_set cc1200_rf_settings;
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_IEEE802154_CC1200_H_ */
