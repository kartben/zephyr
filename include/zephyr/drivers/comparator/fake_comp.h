/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the fake comparator driver API.
 * @ingroup comparator_fake
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_COMPARATOR_FAKE_H_
#define ZEPHYR_INCLUDE_DRIVERS_COMPARATOR_FAKE_H_

#include <zephyr/drivers/comparator.h>
#include <zephyr/fff.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fake comparator driver API functions.
 * @defgroup comparator_fake Fake comparator
 * @ingroup io_emulators
 * @ingroup comparator_interface
 * @{
 */

/**
 * @brief Get the output state of the fake comparator.
 *
 * @see comparator_get_output
 */
DECLARE_FAKE_VALUE_FUNC(int,
			comp_fake_comp_get_output,
			const struct device *);

/**
 * @brief Set the trigger of the fake comparator.
 *
 * @see comparator_set_trigger
 */
DECLARE_FAKE_VALUE_FUNC(int,
			comp_fake_comp_set_trigger,
			const struct device *,
			enum comparator_trigger);

/**
 * @brief Set the trigger callback of the fake comparator.
 *
 * @see comparator_set_trigger_callback
 */
DECLARE_FAKE_VALUE_FUNC(int,
			comp_fake_comp_set_trigger_callback,
			const struct device *,
			comparator_callback_t,
			void *);

/**
 * @brief Check if the trigger of the fake comparator is pending and clear it.
 *
 * @see comparator_trigger_is_pending
 */
DECLARE_FAKE_VALUE_FUNC(int,
			comp_fake_comp_trigger_is_pending,
			const struct device *);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_COMPARATOR_FAKE_H_ */
