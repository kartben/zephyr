/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_MISC_FEETECH_SCS_FEETECH_SCS_BUS_H_
#define ZEPHYR_DRIVERS_MISC_FEETECH_SCS_FEETECH_SCS_BUS_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

#define FEETECH_SCS_INST_PING  0x01
#define FEETECH_SCS_INST_READ  0x02
#define FEETECH_SCS_INST_WRITE 0x03

/* Largest instruction parameter or answer payload the bus handles. */
#define FEETECH_SCS_MAX_PARAMS 16

/**
 * Send an instruction to a servo and wait for its answer.
 *
 * @param bus Bus device.
 * @param id Servo ID.
 * @param instruction Instruction code.
 * @param params Instruction parameters.
 * @param params_len Number of instruction parameters.
 * @param answer Buffer receiving the answer payload, may be NULL if @p answer_len is 0.
 * @param answer_len Expected answer payload length.
 *
 * @retval 0 Success.
 * @retval -EINVAL Parameter or answer too long.
 * @retval -ETIMEDOUT The servo did not answer.
 * @retval -EIO Malformed answer.
 */
int feetech_scs_bus_transceive(const struct device *bus, uint8_t id, uint8_t instruction,
			       const uint8_t *params, size_t params_len, uint8_t *answer,
			       size_t answer_len);

#endif /* ZEPHYR_DRIVERS_MISC_FEETECH_SCS_FEETECH_SCS_BUS_H_ */
