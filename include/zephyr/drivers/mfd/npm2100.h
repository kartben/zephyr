/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the nPM2100 MFD driver.
 * @ingroup mdf_interface_npm2100
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MFD_NPM2100_H_
#define ZEPHYR_INCLUDE_DRIVERS_MFD_NPM2100_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup mdf_interface_npm2100 MFD NPM2100 Interface
 * @ingroup mfd_interfaces
 * @{
 */

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

/**
 * @brief npm2100 event sources
 *
 * Events are selected in the pin mask of a @ref gpio_callback, and reported
 * to the callback handler, using their bit position, i.e. BIT(event).
 */
enum mfd_npm2100_event {
	NPM2100_EVENT_SYS_DIETEMP_WARN,  /**< Die temperature warning */
	NPM2100_EVENT_SYS_SHIPHOLD_FALL, /**< Falling edge on SHPHLD pin */
	NPM2100_EVENT_SYS_SHIPHOLD_RISE, /**< Rising edge on SHPHLD pin */
	NPM2100_EVENT_SYS_PGRESET_FALL,  /**< Falling edge on PG/RESET pin */
	NPM2100_EVENT_SYS_PGRESET_RISE,  /**< Rising edge on PG/RESET pin */
	NPM2100_EVENT_SYS_TIMER_EXPIRY,  /**< Timer expired */
	NPM2100_EVENT_ADC_VBAT_READY,    /**< Battery voltage measurement ready */
	NPM2100_EVENT_ADC_DIETEMP_READY, /**< Die temperature measurement ready */
	NPM2100_EVENT_ADC_DROOP_DETECT,  /**< Voltage droop detected */
	NPM2100_EVENT_ADC_VOUT_READY,    /**< Output voltage measurement ready */
	NPM2100_EVENT_GPIO0_FALL,        /**< Falling edge on GPIO0 pin */
	NPM2100_EVENT_GPIO0_RISE,        /**< Rising edge on GPIO0 pin */
	NPM2100_EVENT_GPIO1_FALL,        /**< Falling edge on GPIO1 pin */
	NPM2100_EVENT_GPIO1_RISE,        /**< Rising edge on GPIO1 pin */
	NPM2100_EVENT_BOOST_VBAT_WARN,   /**< Battery voltage warning threshold crossed */
	NPM2100_EVENT_BOOST_VOUT_MIN,    /**< Output voltage minimum threshold crossed */
	NPM2100_EVENT_BOOST_VOUT_WARN,   /**< Output voltage warning threshold crossed */
	NPM2100_EVENT_BOOST_VOUT_DPS,    /**< Output voltage DPS threshold crossed */
	NPM2100_EVENT_BOOST_VOUT_OK,     /**< Output voltage in normal range */
	NPM2100_EVENT_LDOSW_OCP,         /**< LDOSW overcurrent protection triggered */
	NPM2100_EVENT_LDOSW_VINTFAIL,    /**< LDOSW VINT failure */
	NPM2100_EVENT_MAX                /**< Number of events */
};

/** @brief npm2100 timer modes */
enum mfd_npm2100_timer_mode {
	NPM2100_TIMER_MODE_GENERAL_PURPOSE, /**< General purpose timer */
	NPM2100_TIMER_MODE_WDT_RESET,       /**< Watchdog timer, full power reset on expiry */
	NPM2100_TIMER_MODE_WDT_POWER_CYCLE, /**< Watchdog timer, power cycle on expiry */
	NPM2100_TIMER_MODE_WAKEUP,          /**< Wakeup timer, wake from hibernate on expiry */
};

/**
 * @brief Write npm2100 timer register
 *
 * The timer tick resolution is 1/64 seconds.
 * This function does not start the timer (see mfd_npm2100_start_timer()).
 *
 * @param dev npm2100 mfd device
 * @param time_ms timer value in ms
 * @param mode timer mode
 * @return 0 on success, negative errno value on failure (see i2c_write_dt()).
 * @retval -EINVAL Time value is too large.
 */
int mfd_npm2100_set_timer(const struct device *dev, uint32_t time_ms,
			  enum mfd_npm2100_timer_mode mode);

/**
 * @brief Start npm2100 timer
 *
 * @param dev npm2100 mfd device
 * @return 0 on success, negative errno value on failure (see i2c_write_dt()).
 */
int mfd_npm2100_start_timer(const struct device *dev);

/**
 * @brief npm2100 full power reset
 *
 * @param dev npm2100 mfd device
 * @return 0 on success, negative errno value on failure (see i2c_write_dt()).
 */
int mfd_npm2100_reset(const struct device *dev);

/**
 * @brief npm2100 hibernate
 *
 * Enters low power state, and wakes after specified time or "shphld" pin signal.
 * Pass-through mode can be used when the battery voltage is high enough to supply the pmic directly
 * without boosting. This lowers the power consumption of the pmic when hibernate mode is active.
 *
 * @param dev npm2100 mfd device
 * @param time_ms timer value in ms. Set to 0 to disable timer.
 * @param pass_through set to use pass-through hibernate mode.
 * @return 0 on success, negative errno value on failure (see i2c_write_dt()).
 * @retval -EINVAL Time value is too large.
 * @retval -EBUSY The timer is already in use.
 */
int mfd_npm2100_hibernate(const struct device *dev, uint32_t time_ms, bool pass_through);

/**
 * @brief Add npm2100 event callback
 *
 * @param dev npm2100 mfd device
 * @param callback callback
 * @return 0 on success, negative errno value on failure.
 */
int mfd_npm2100_add_callback(const struct device *dev, struct gpio_callback *callback);

/**
 * @brief Remove npm2100 event callback
 *
 * @param dev npm2100 mfd device
 * @param callback callback
 * @return 0 on success, negative errno value on failure.
 */
int mfd_npm2100_remove_callback(const struct device *dev, struct gpio_callback *callback);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_MFD_NPM2100_H_ */
