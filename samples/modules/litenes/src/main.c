/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "fce.h"

LOG_MODULE_REGISTER(litenes, LOG_LEVEL_INF);

static const unsigned char rom_data[] = {
#include <turtles_nes.inc>
};

int main(void)
{
	LOG_INF("LiteNES - NES Emulator for Zephyr");
	LOG_INF("ROM size: %zu bytes", sizeof(rom_data));

	if (fce_load_rom((char *)rom_data) != 0) {
		LOG_ERR("Failed to load ROM");
		return -1;
	}

	LOG_INF("ROM loaded, starting emulation...");
	fce_init();
	fce_run();

	return 0;
}
