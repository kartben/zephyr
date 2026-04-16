/*
 * Copyright (c) 2024 Cienet
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup sensor_clock_interface
 * @brief Public API for sensor clock helpers.
 */

#ifndef ZEPHYR_DRIVERS_SENSOR_CLOCK_H_
#define ZEPHYR_DRIVERS_SENSOR_CLOCK_H_

/**
 * @brief Monotonic time base for sensor data.
 * @defgroup sensor_clock_interface Sensor Clock
 * @since 4.1
 * @version 0.1.0
 * @ingroup io_interfaces
 * @{
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Retrieve the current sensor clock cycles.
 *
 * This function obtains the current cycle count from the selected
 * sensor clock source. The clock source may be the system clock or
 * an external clock, depending on the configuration.
 *
 * @kconfig_dep{CONFIG_SENSOR_CLOCK}
 *
 * @param[out] cycles Pointer to a 64-bit unsigned integer where the
 *                    current clock cycle count will be stored.
 *
 * @retval 0 Success.
 * @retval -errno Negative error code on failure.
 */
int sensor_clock_get_cycles(uint64_t *cycles);

/**
 * @brief Convert sensor clock cycles to nanoseconds.
 *
 * This function converts clock cycles into nanoseconds based on the
 * clock frequency.
 *
 * @kconfig_dep{CONFIG_SENSOR_CLOCK}
 *
 * @param cycles Clock cycles to convert.
 * @return Time in nanoseconds.
 */
uint64_t sensor_clock_cycles_to_ns(uint64_t cycles);

#ifdef __cplusplus
}
#endif

/** @} */

#endif /* ZEPHYR_DRIVERS_SENSOR_CLOCK_H_ */
