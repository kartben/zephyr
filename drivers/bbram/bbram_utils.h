/*
 * Copyright (c) 2026 Benjamin Cabé <benjamin@zephyrproject.org>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_BBRAM_BBRAM_UTILS_H_
#define ZEPHYR_DRIVERS_BBRAM_BBRAM_UTILS_H_

#include <stdbool.h>
#include <stddef.h>

#include <zephyr/sys/math_extras.h>

/**
 * @brief Validate a BBRAM offset and size against a maximum size.
 *
 * Rejects zero-length accesses and uses an overflow-safe end calculation so
 * large @p offset / @p size values cannot wrap past @p max_size.
 *
 * @param offset Start offset into BBRAM
 * @param size Number of bytes to access (must be >= 1)
 * @param max_size Total BBRAM size in bytes
 *
 * @return true if the range [offset, offset + size) is within [0, max_size)
 */
static inline bool bbram_offset_is_valid(size_t offset, size_t size, size_t max_size)
{
	size_t end;

	if (size < 1 || size_add_overflow(offset, size, &end)) {
		return false;
	}

	return end <= max_size;
}

#endif /* ZEPHYR_DRIVERS_BBRAM_BBRAM_UTILS_H_ */
