/*
 * Copyright (c) 2022 Vestas Wind Systems A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Fake CAN controller driver API for testing purposes.
 * @ingroup can_fake
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_CAN_CAN_FAKE_H_
#define ZEPHYR_INCLUDE_DRIVERS_CAN_CAN_FAKE_H_

#include <zephyr/drivers/can.h>
#include <zephyr/fff.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fake CAN controller driver API functions.
 * @defgroup can_fake Fake CAN controller
 * @ingroup io_emulators
 * @ingroup can_interface
 * @{
 */

/**
 * @brief Start the fake CAN controller.
 *
 * @see can_start
 */
DECLARE_FAKE_VALUE_FUNC(int, fake_can_start, const struct device *);

/**
 * @brief Stop the fake CAN controller.
 *
 * @see can_stop
 */
DECLARE_FAKE_VALUE_FUNC(int, fake_can_stop, const struct device *);

/**
 * @brief Configure the bus timing of the fake CAN controller.
 *
 * @see can_set_timing
 */
DECLARE_FAKE_VALUE_FUNC(int, fake_can_set_timing, const struct device *, const struct can_timing *);

/**
 * @brief Configure the bus timing for the data phase of the fake CAN controller.
 *
 * @see can_set_timing_data
 */
DECLARE_FAKE_VALUE_FUNC(int, fake_can_set_timing_data, const struct device *,
			const struct can_timing *);

/**
 * @brief Get the supported modes of the fake CAN controller.
 *
 * @see can_get_capabilities
 */
DECLARE_FAKE_VALUE_FUNC(int, fake_can_get_capabilities, const struct device *, can_mode_t *);

/**
 * @brief Set the operation mode of the fake CAN controller.
 *
 * @see can_set_mode
 */
DECLARE_FAKE_VALUE_FUNC(int, fake_can_set_mode, const struct device *, can_mode_t);

/**
 * @brief Queue a CAN frame for transmission on the fake CAN controller.
 *
 * @see can_send
 */
DECLARE_FAKE_VALUE_FUNC(int, fake_can_send, const struct device *, const struct can_frame *,
			k_timeout_t, can_tx_callback_t, void *);

/**
 * @brief Add an RX filter to the fake CAN controller.
 *
 * @see can_add_rx_filter
 */
DECLARE_FAKE_VALUE_FUNC(int, fake_can_add_rx_filter, const struct device *, can_rx_callback_t,
			void *, const struct can_filter *);

/**
 * @brief Remove an RX filter from the fake CAN controller.
 *
 * @see can_remove_rx_filter
 */
DECLARE_FAKE_VOID_FUNC(fake_can_remove_rx_filter, const struct device *, int);

/**
 * @brief Recover the fake CAN controller from bus-off state.
 *
 * @see can_recover
 */
DECLARE_FAKE_VALUE_FUNC(int, fake_can_recover, const struct device *, k_timeout_t);

/**
 * @brief Get the state of the fake CAN controller.
 *
 * @see can_get_state
 */
DECLARE_FAKE_VALUE_FUNC(int, fake_can_get_state, const struct device *, enum can_state *,
			struct can_bus_err_cnt *);

/**
 * @brief Set a state change callback for the fake CAN controller.
 *
 * @see can_set_state_change_callback
 */
DECLARE_FAKE_VOID_FUNC(fake_can_set_state_change_callback, const struct device *,
		       can_state_change_callback_t, void *);

/**
 * @brief Get the maximum number of RX filters supported by the fake CAN controller.
 *
 * @see can_get_max_filters
 */
DECLARE_FAKE_VALUE_FUNC(int, fake_can_get_max_filters, const struct device *, bool);

/**
 * @brief Get the core clock rate of the fake CAN controller.
 *
 * @see can_get_core_clock
 */
DECLARE_FAKE_VALUE_FUNC(int, fake_can_get_core_clock, const struct device *, uint32_t *);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_CAN_CAN_FAKE_H_ */
