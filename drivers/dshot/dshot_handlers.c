/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/dshot.h>
#include <zephyr/internal/syscall_handler.h>

static inline int z_vrfy_dshot_configure(const struct device *dev, const struct dshot_config *cfg)
{
	struct dshot_config cfg_copy;

	K_OOPS(K_SYSCALL_DRIVER_DSHOT(dev, configure));
	K_OOPS(k_usermode_from_copy(&cfg_copy, cfg, sizeof(cfg_copy)));
	return z_impl_dshot_configure(dev, &cfg_copy);
}
#include <zephyr/syscalls/dshot_configure_mrsh.c>

static inline int z_vrfy_dshot_set_throttle(const struct device *dev, uint8_t channel,
					    uint16_t throttle, bool telemetry)
{
	K_OOPS(K_SYSCALL_DRIVER_DSHOT(dev, set_frame));
	return z_impl_dshot_set_throttle(dev, channel, throttle, telemetry);
}
#include <zephyr/syscalls/dshot_set_throttle_mrsh.c>

static inline int z_vrfy_dshot_send(const struct device *dev)
{
	K_OOPS(K_SYSCALL_DRIVER_DSHOT(dev, send));
	return z_impl_dshot_send(dev);
}
#include <zephyr/syscalls/dshot_send_mrsh.c>

static inline int z_vrfy_dshot_stop(const struct device *dev)
{
	K_OOPS(K_SYSCALL_DRIVER_DSHOT(dev, stop));
	return z_impl_dshot_stop(dev);
}
#include <zephyr/syscalls/dshot_stop_mrsh.c>

static inline int z_vrfy_dshot_send_command(const struct device *dev, uint32_t channels,
					    enum dshot_command cmd)
{
	K_OOPS(K_SYSCALL_DRIVER_DSHOT(dev, send_command));
	return z_impl_dshot_send_command(dev, channels, cmd);
}
#include <zephyr/syscalls/dshot_send_command_mrsh.c>

static inline uint8_t z_vrfy_dshot_get_channel_count(const struct device *dev)
{
	K_OOPS(K_SYSCALL_DRIVER_DSHOT(dev, get_channel_count));
	return z_impl_dshot_get_channel_count(dev);
}
#include <zephyr/syscalls/dshot_get_channel_count_mrsh.c>
