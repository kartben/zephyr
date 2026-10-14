/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_SENSOR_GOERTEK_SPA06_SPA06_EMUL_H_
#define ZEPHYR_DRIVERS_SENSOR_GOERTEK_SPA06_SPA06_EMUL_H_

#include <stdint.h>
#include <zephyr/drivers/emul.h>

#include "spa06.h"

/**
 * @brief Set the calibration coefficients the emulated sensor reports.
 *
 * The driver reads them during initialization.
 *
 * @param target Emulator instance.
 * @param coef Coefficients, within the register widths.
 */
void spa06_emul_set_coef(const struct emul *target, const struct spa06_coef *coef);

/**
 * @brief Set the raw pressure and temperature results of the next measurements.
 *
 * In continuous mode, the results are available at once.
 *
 * @param target Emulator instance.
 * @param raw_prs Raw 24-bit pressure result.
 * @param raw_tmp Raw 24-bit temperature result.
 */
void spa06_emul_set_raw(const struct emul *target, int32_t raw_prs, int32_t raw_tmp);

/**
 * @brief Append an entry to the emulated FIFO.
 *
 * @param target Emulator instance.
 * @param entry Raw 24-bit entry, with bit 0 set for pressure.
 *
 * @retval 0 Entry added.
 * @retval -ENOSPC FIFO full.
 */
int spa06_emul_fifo_push(const struct emul *target, uint32_t entry);

/**
 * @brief Set the interrupt status flags, cleared when the driver reads them.
 *
 * @param target Emulator instance.
 * @param int_sts INT_STS register value.
 */
void spa06_emul_set_int_status(const struct emul *target, uint8_t int_sts);

/**
 * @brief Read an emulated register without side effects.
 *
 * @param target Emulator instance.
 * @param reg Register address.
 *
 * @return Register value.
 */
uint8_t spa06_emul_get_reg(const struct emul *target, uint8_t reg);

#endif /* ZEPHYR_DRIVERS_SENSOR_GOERTEK_SPA06_SPA06_EMUL_H_ */
