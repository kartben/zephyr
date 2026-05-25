/*
 * Copyright 2024 Google LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT input_keymap

#include <zephyr/device.h>
#include <zephyr/dt-bindings/input/keymap.h>
#include <zephyr/input/input.h>
#include <zephyr/input/input_hid.h>
#include <zephyr/input/input_keymap.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/usb/class/hid.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(input_keymap, CONFIG_INPUT_LOG_LEVEL);

struct keymap_layer_config {
	const uint16_t *codes;
	uint32_t num_codes;
	uint8_t modifier_mask;
};

struct keymap_config {
	const struct device *input_dev;
	const uint16_t *codes;
	const struct keymap_layer_config *layers;
	uint32_t num_codes;
	uint8_t row_size;
	uint8_t col_size;
	uint8_t num_layers;
};

struct keymap_data {
	uint32_t row;
	uint32_t col;
	bool pressed;
	uint8_t active_modifiers;
	uint8_t modifier_refcounts[8];
	uint16_t *pressed_codes;
};

static int keymap_modifier_index(uint8_t modifier)
{
	switch (modifier) {
	case HID_KBD_MODIFIER_LEFT_CTRL:
		return 0;
	case HID_KBD_MODIFIER_LEFT_SHIFT:
		return 1;
	case HID_KBD_MODIFIER_LEFT_ALT:
		return 2;
	case HID_KBD_MODIFIER_LEFT_UI:
		return 3;
	case HID_KBD_MODIFIER_RIGHT_CTRL:
		return 4;
	case HID_KBD_MODIFIER_RIGHT_SHIFT:
		return 5;
	case HID_KBD_MODIFIER_RIGHT_ALT:
		return 6;
	case HID_KBD_MODIFIER_RIGHT_UI:
		return 7;
	default:
		return -EINVAL;
	}
}

static void keymap_update_modifiers(struct keymap_data *data, uint16_t code, bool pressed)
{
	uint8_t modifier = input_to_hid_modifier(code);
	int index;

	if (modifier == HID_KBD_MODIFIER_NONE) {
		return;
	}

	index = keymap_modifier_index(modifier);
	if (index < 0) {
		return;
	}

	if (pressed) {
		if (data->modifier_refcounts[index] < UINT8_MAX) {
			data->modifier_refcounts[index]++;
		}

		data->active_modifiers |= modifier;
		return;
	}

	if (data->modifier_refcounts[index] == 0U) {
		return;
	}

	data->modifier_refcounts[index]--;
	if (data->modifier_refcounts[index] == 0U) {
		data->active_modifiers &= ~modifier;
	}
}

static uint16_t keymap_resolve_code(const struct keymap_config *cfg,
				    uint8_t active_modifiers,
				    uint32_t offset)
{
	uint16_t code = 0U;
	uint8_t best_match_bits = 0U;

	if (offset < cfg->num_codes) {
		code = cfg->codes[offset];
	}

	for (int i = 0; i < cfg->num_layers; i++) {
		const struct keymap_layer_config *layer = &cfg->layers[i];
		uint8_t match_bits;
		uint16_t layer_code;

		if ((active_modifiers & layer->modifier_mask) != layer->modifier_mask) {
			continue;
		}

		if (offset >= layer->num_codes) {
			continue;
		}

		layer_code = layer->codes[offset];
		if (layer_code == 0U) {
			continue;
		}

		match_bits = POPCOUNT(layer->modifier_mask);
		if (match_bits > best_match_bits) {
			code = layer_code;
			best_match_bits = match_bits;
		}
	}

	return code;
}

static void keymap_cb(struct input_event *evt, void *user_data)
{
	const struct device *dev = user_data;
	const struct keymap_config *cfg = dev->config;
	struct keymap_data *data = dev->data;
	uint16_t code;
	uint32_t offset;

	switch (evt->code) {
	case INPUT_ABS_X:
		data->col = evt->value;
		break;
	case INPUT_ABS_Y:
		data->row = evt->value;
		break;
	case INPUT_BTN_TOUCH:
		data->pressed = evt->value;
		break;
	}

	if (!evt->sync) {
		return;
	}

	if (data->row >= cfg->row_size ||
	    data->col >= cfg->col_size) {
		LOG_WRN("keymap event out of range: row=%u col=%u", data->row, data->col);
		return;
	}

	offset = (data->row * cfg->col_size) + data->col;

	if (data->pressed) {
		code = keymap_resolve_code(cfg, data->active_modifiers, offset);
		if (code == 0U) {
			LOG_DBG("keymap event undefined: row=%u col=%u", data->row, data->col);
			return;
		}

		data->pressed_codes[offset] = code;
		keymap_update_modifiers(data, code, true);
	} else {
		code = data->pressed_codes[offset];
		if (code == 0U) {
			LOG_DBG("keymap release undefined: row=%u col=%u", data->row, data->col);
			return;
		}

		data->pressed_codes[offset] = 0U;
		keymap_update_modifiers(data, code, false);
	}

	LOG_DBG("input event: %3u %3u %d code=%u", data->row, data->col,
		data->pressed, code);

	input_report_key(dev, code, data->pressed, true, K_FOREVER);
}

static int keymap_init(const struct device *dev)
{
	const struct keymap_config *cfg = dev->config;

	if (!device_is_ready(cfg->input_dev)) {
		LOG_ERR("input device not ready");
		return -ENODEV;
	}

	return 0;
}

#define KEYMAP_ENTRY_OFFSET(keymap_entry, col_size) \
	(MATRIX_ROW(keymap_entry) * col_size + MATRIX_COL(keymap_entry))

#define KEYMAP_ENTRY_VALIDATE(node_id, prop, idx, row_size, col_size)		\
	BUILD_ASSERT(MATRIX_ROW(DT_PROP_BY_IDX(node_id, prop, idx)) < row_size,	\
		     "invalid row");						\
	BUILD_ASSERT(MATRIX_COL(DT_PROP_BY_IDX(node_id, prop, idx)) < col_size,	\
		     "invalid col");

#define CODES_INIT(node_id, prop, idx, col_size) \
	[KEYMAP_ENTRY_OFFSET(DT_PROP_BY_IDX(node_id, prop, idx), col_size)] = \
		MATRIX_CODE(DT_PROP_BY_IDX(node_id, prop, idx)),

#define KEYMAP_MODIFIER_CODE_MASK(code)						\
	((code) == INPUT_KEY_LEFTCTRL ? HID_KBD_MODIFIER_LEFT_CTRL :		\
	 (code) == INPUT_KEY_LEFTSHIFT ? HID_KBD_MODIFIER_LEFT_SHIFT :	\
	 (code) == INPUT_KEY_LEFTALT ? HID_KBD_MODIFIER_LEFT_ALT :		\
	 (code) == INPUT_KEY_LEFTMETA ? HID_KBD_MODIFIER_LEFT_UI :		\
	 (code) == INPUT_KEY_RIGHTCTRL ? HID_KBD_MODIFIER_RIGHT_CTRL :	\
	 (code) == INPUT_KEY_RIGHTSHIFT ? HID_KBD_MODIFIER_RIGHT_SHIFT :	\
	 (code) == INPUT_KEY_RIGHTALT ? HID_KBD_MODIFIER_RIGHT_ALT :	\
	 (code) == INPUT_KEY_RIGHTMETA ? HID_KBD_MODIFIER_RIGHT_UI :	\
	 HID_KBD_MODIFIER_NONE)

#define KEYMAP_LAYER_MODIFIER_VALIDATE(node_id, prop, idx)				\
	BUILD_ASSERT(KEYMAP_MODIFIER_CODE_MASK(DT_PROP_BY_IDX(node_id, prop, idx)) !=	\
			     HID_KBD_MODIFIER_NONE,					\
		     "modifier-codes must contain keyboard modifiers");

#define KEYMAP_LAYER_MODIFIER_MASK_ENTRY(node_id, prop, idx)			\
	| KEYMAP_MODIFIER_CODE_MASK(DT_PROP_BY_IDX(node_id, prop, idx))

#define KEYMAP_LAYER_DEFINE(node_id, inst)						\
	DT_FOREACH_PROP_ELEM(node_id, modifier_codes, KEYMAP_LAYER_MODIFIER_VALIDATE)	\
	DT_FOREACH_PROP_ELEM_VARGS(node_id, keymap, KEYMAP_ENTRY_VALIDATE,		\
				   DT_INST_PROP(inst, row_size),			\
				   DT_INST_PROP(inst, col_size))			\
	static const uint16_t keymap_layer_codes_##node_id[] = {			\
		DT_FOREACH_PROP_ELEM_VARGS(node_id, keymap, CODES_INIT,		\
					   DT_INST_PROP(inst, col_size))		\
	};

#define KEYMAP_LAYER_CONFIG_INIT(node_id)						\
	{										\
		.codes = keymap_layer_codes_##node_id,					\
		.num_codes = ARRAY_SIZE(keymap_layer_codes_##node_id),			\
		.modifier_mask = (0 DT_FOREACH_PROP_ELEM(node_id, modifier_codes,	\
							 KEYMAP_LAYER_MODIFIER_MASK_ENTRY)),\
	},

#define KEYMAP_LAYER_ARRAY_DEFINE(inst)						\
	COND_CODE_1(UTIL_BOOL(DT_INST_CHILD_NUM_STATUS_OKAY(inst)),			\
		    (DT_INST_FOREACH_CHILD_STATUS_OKAY_VARGS(inst, KEYMAP_LAYER_DEFINE, inst) \
		     static const struct keymap_layer_config keymap_layers_##inst[] = { \
			     DT_INST_FOREACH_CHILD_STATUS_OKAY(inst, KEYMAP_LAYER_CONFIG_INIT) \
		     };),								\
		    ())

#define INPUT_KEYMAP_DEFINE(inst)								\
	INPUT_CALLBACK_DEFINE_NAMED(DEVICE_DT_GET(DT_INST_PARENT(inst)), keymap_cb,		\
				    (void *)DEVICE_DT_INST_GET(inst), keymap_cb_##inst);	\
												\
	DT_INST_FOREACH_PROP_ELEM_VARGS(inst, keymap, KEYMAP_ENTRY_VALIDATE,			\
					DT_INST_PROP(inst, row_size),				\
					DT_INST_PROP(inst, col_size))				\
												\
	static const uint16_t keymap_codes_##inst[] = {						\
		DT_INST_FOREACH_PROP_ELEM_VARGS(inst, keymap, CODES_INIT,			\
						DT_INST_PROP(inst, col_size))			\
	};											\
												\
	KEYMAP_LAYER_ARRAY_DEFINE(inst)								\
												\
	static const struct keymap_config keymap_config_##inst = {				\
		.input_dev = DEVICE_DT_GET(DT_INST_PARENT(inst)),				\
		.codes = keymap_codes_##inst,							\
		.layers = COND_CODE_1(UTIL_BOOL(DT_INST_CHILD_NUM_STATUS_OKAY(inst)),		\
				      (keymap_layers_##inst), (NULL)),			\
		.num_codes = ARRAY_SIZE(keymap_codes_##inst),					\
		.row_size = DT_INST_PROP(inst, row_size),					\
		.col_size = DT_INST_PROP(inst, col_size),					\
		.num_layers = DT_INST_CHILD_NUM_STATUS_OKAY(inst),				\
	};											\
												\
	static uint16_t keymap_pressed_codes_##inst[DT_INST_PROP(inst, row_size) *		\
						    DT_INST_PROP(inst, col_size)];		\
	static struct keymap_data keymap_data_##inst = {					\
		.pressed_codes = keymap_pressed_codes_##inst,					\
	};											\
												\
	DEVICE_DT_INST_DEFINE(inst, keymap_init, NULL,						\
			      &keymap_data_##inst, &keymap_config_##inst,			\
			      POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(INPUT_KEYMAP_DEFINE)
