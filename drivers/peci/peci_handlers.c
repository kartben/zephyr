/*
 * Copyright (c) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/peci.h>
#include <zephyr/internal/syscall_handler.h>


static inline int z_vrfy_peci_config(const struct device *dev,
				     uint32_t bitrate)
{
	K_OOPS(K_SYSCALL_DRIVER_PECI(dev, config));

	return z_impl_peci_config(dev, bitrate);
}
#include <zephyr/syscalls/peci_config_mrsh.c>

static inline int z_vrfy_peci_enable(const struct device *dev)
{
	K_OOPS(K_SYSCALL_DRIVER_PECI(dev, enable));

	return z_impl_peci_enable(dev);
}
#include <zephyr/syscalls/peci_enable_mrsh.c>

static inline int z_vrfy_peci_disable(const struct device *dev)
{
	K_OOPS(K_SYSCALL_DRIVER_PECI(dev, disable));

	return z_impl_peci_disable(dev);
}
#include <zephyr/syscalls/peci_disable_mrsh.c>

static inline int z_vrfy_peci_transfer(const struct device *dev,
				       struct peci_msg *msg)
{
	struct peci_msg msg_copy;

	K_OOPS(K_SYSCALL_DRIVER_PECI(dev, transfer));
	K_OOPS(k_usermode_from_copy(&msg_copy, msg, sizeof(*msg)));

	/**
	 * k_usermode_from_copy() only duplicates the outer struct; the buffer
	 * pointers it carries are still caller supplied and the driver
	 * dereferences them in supervisor mode, so they must be checked here.
	 */
	if (msg_copy.tx_buffer.len > 1U) {
		/* Most drivers read len - 1 payload bytes, the eSPI PECI
		 * driver reads len, so check for the larger of the two.
		 */
		K_OOPS(K_SYSCALL_MEMORY_READ(msg_copy.tx_buffer.buf,
					     msg_copy.tx_buffer.len));
	}

	/* The drivers write rx_buffer.len payload bytes plus the FCS byte
	 * regardless of the buffer pointer, so a NULL buffer is only fine
	 * when there is nothing to receive and the length is zero.
	 */
	if ((msg_copy.rx_buffer.buf != NULL) || (msg_copy.rx_buffer.len != 0U)) {
		size_t rx_len;

		K_OOPS(K_SYSCALL_VERIFY_MSG(!size_add_overflow(msg_copy.rx_buffer.len,
							       1U, &rx_len),
					    "rx_buffer.len overflow"));
		K_OOPS(K_SYSCALL_MEMORY_WRITE(msg_copy.rx_buffer.buf, rx_len));
	}

	return z_impl_peci_transfer(dev, &msg_copy);
}
#include <zephyr/syscalls/peci_transfer_mrsh.c>
