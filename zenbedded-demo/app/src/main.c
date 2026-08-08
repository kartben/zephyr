/*
 * Copyright (c) 2026 zenbedded contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(zenbedded, CONFIG_ZENBEDDED_LOG_LEVEL);

int main(void)
{
	LOG_INF("zenbedded skeleton up on %s", CONFIG_BOARD_TARGET);

	return 0;
}
