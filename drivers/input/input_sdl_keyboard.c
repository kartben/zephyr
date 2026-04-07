/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_input_sdl_keyboard

#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include "input_sdl_keyboard_bottom.h"

LOG_MODULE_REGISTER(sdl_keyboard, CONFIG_INPUT_LOG_LEVEL);

static void sdl_keyboard_callback(struct sdl_keyboard_data *data)
{
	input_report_key(data->dev, data->key_code, data->pressed ? 1 : 0, true, K_FOREVER);
}

static int sdl_keyboard_init(const struct device *dev)
{
	struct sdl_keyboard_data *data = dev->data;

	LOG_INF("Init '%s' device", dev->name);

	data->dev = dev;
	data->callback = sdl_keyboard_callback;
	sdl_keyboard_init_bottom(data);

	return 0;
}

#define INPUT_SDL_KEYBOARD_DEFINE(inst)                                                            \
	static struct sdl_keyboard_data sdl_keyboard_data_##inst;                                  \
	DEVICE_DT_INST_DEFINE(inst, sdl_keyboard_init, NULL, &sdl_keyboard_data_##inst, NULL,      \
			      POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(INPUT_SDL_KEYBOARD_DEFINE)
