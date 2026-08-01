/*
 * Copyright (c) 2025 Georgij Cernysiov <geo.cgv@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the STM32 FMC memory controller driver APIs.
 * @ingroup memc_interface
 */

#ifndef ZEPHYR_INCLUDE_MEMC_STM32_H_
#define ZEPHYR_INCLUDE_MEMC_STM32_H_

/**
 * @brief Get the FMC clock rate.
 *
 * @param[out] rate FMC clock rate, in Hz.
 *
 * @retval 0 On success.
 * @retval -EIO If the clock rate could not be retrieved.
 */
int memc_stm32_fmc_clock_rate(uint32_t *rate);

#endif /* ZEPHYR_INCLUDE_MEMC_STM32_H_ */
