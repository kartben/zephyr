/*
 * Copyright (c) 2025 Georgij Cernysiov <geo.cgv@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup io_interfaces
 * @brief STM32 memory-controller helper APIs.
 */

#ifndef ZEPHYR_INCLUDE_MEMC_STM32_H_
#define ZEPHYR_INCLUDE_MEMC_STM32_H_

/**
 * @brief Get the STM32 FMC clock rate.
 *
 * @param[out] rate Pointer to the destination for the clock rate, in Hz.
 *
 * @retval 0 If the clock rate was retrieved successfully.
 * @retval -errno Negative error code on failure.
 */
int memc_stm32_fmc_clock_rate(uint32_t *rate);

#endif /* ZEPHYR_INCLUDE_MEMC_STM32_H_ */
