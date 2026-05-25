/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_palette_dither_display

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>

#include "display_color_palette.h"

LOG_MODULE_REGISTER(display_palette_dither, CONFIG_DISPLAY_LOG_LEVEL);

struct palette_dither_display_config {
	const struct device *display;
	enum display_pixel_format initial_pixel_format;
	size_t converted_buf_size;
};

struct palette_dither_display_data {
	enum display_pixel_format current_pixel_format;
	uint8_t *converted_buf;
};

static bool palette_dither_display_pixel_format_supported(enum display_pixel_format pixel_format)
{
	switch (pixel_format) {
	case PIXEL_FORMAT_I_4:
	case PIXEL_FORMAT_RGB_565:
	case PIXEL_FORMAT_RGB_565X:
	case PIXEL_FORMAT_RGB_888:
		return true;
	default:
		return false;
	}
}

static int palette_dither_display_set_wrapped_pixel_format(const struct device *display)
{
	struct display_capabilities caps;
	int ret;

	display_get_capabilities(display, &caps);
	if ((caps.supported_pixel_formats & PIXEL_FORMAT_I_4) == 0U) {
		return -ENOTSUP;
	}

	if (caps.current_pixel_format == PIXEL_FORMAT_I_4) {
		return 0;
	}

	ret = display_set_pixel_format(display, PIXEL_FORMAT_I_4);
	if ((ret == -ENOSYS) && (caps.current_pixel_format == PIXEL_FORMAT_I_4)) {
		return 0;
	}

	return ret;
}

static int palette_dither_display_blanking_on(const struct device *dev)
{
	const struct palette_dither_display_config *config = dev->config;

	return display_blanking_on(config->display);
}

static int palette_dither_display_blanking_off(const struct device *dev)
{
	const struct palette_dither_display_config *config = dev->config;

	return display_blanking_off(config->display);
}

static int palette_dither_display_write(const struct device *dev, const uint16_t x,
					const uint16_t y,
					const struct display_buffer_descriptor *desc,
					const void *buf)
{
	const struct palette_dither_display_config *config = dev->config;
	struct palette_dither_display_data *data = dev->data;
	struct display_capabilities caps;
	struct display_buffer_descriptor i4_desc;
	uint32_t palette[CONFIG_DISPLAY_COLOR_PALETTE_MAX_SIZE];
	size_t converted_size;
	int ret;

	if (data->current_pixel_format == PIXEL_FORMAT_I_4) {
		return display_write(config->display, x, y, desc, buf);
	}

	converted_size = DISPLAY_COLOR_PALETTE_I4_BUFFER_SIZE(desc->width, desc->height);
	if (converted_size > config->converted_buf_size) {
		return -EINVAL;
	}

	display_get_capabilities(config->display, &caps);
	memcpy(palette, caps.color_palette, sizeof(palette));
	ret = display_color_palette_convert_to_i4(desc, data->current_pixel_format, buf,
						  palette, ARRAY_SIZE(palette),
						  data->converted_buf, converted_size);
	if (ret < 0) {
		return ret;
	}

	i4_desc = *desc;
	i4_desc.buf_size = converted_size;
	i4_desc.pitch = desc->width;

	return display_write(config->display, x, y, &i4_desc, data->converted_buf);
}

static int palette_dither_display_read(const struct device *dev, const uint16_t x,
				       const uint16_t y,
				       const struct display_buffer_descriptor *desc, void *buf)
{
	const struct palette_dither_display_config *config = dev->config;
	struct palette_dither_display_data *data = dev->data;
	struct display_capabilities caps;
	struct display_buffer_descriptor i4_desc;
	uint32_t palette[CONFIG_DISPLAY_COLOR_PALETTE_MAX_SIZE];
	size_t converted_size;
	int ret;

	if (data->current_pixel_format == PIXEL_FORMAT_I_4) {
		return display_read(config->display, x, y, desc, buf);
	}

	converted_size = DISPLAY_COLOR_PALETTE_I4_BUFFER_SIZE(desc->width, desc->height);
	if (converted_size > config->converted_buf_size) {
		return -EINVAL;
	}

	i4_desc = *desc;
	i4_desc.buf_size = converted_size;
	i4_desc.pitch = desc->width;

	ret = display_read(config->display, x, y, &i4_desc, data->converted_buf);
	if (ret < 0) {
		return ret;
	}

	display_get_capabilities(config->display, &caps);
	memcpy(palette, caps.color_palette, sizeof(palette));
	return display_color_palette_convert_from_i4(&i4_desc, data->current_pixel_format,
						     data->converted_buf, palette,
						     ARRAY_SIZE(palette), buf,
						     desc->buf_size);
}

static int palette_dither_display_clear(const struct device *dev)
{
	const struct palette_dither_display_config *config = dev->config;

	return display_clear(config->display);
}

static void *palette_dither_display_get_framebuffer(const struct device *dev)
{
	const struct palette_dither_display_config *config = dev->config;
	const struct palette_dither_display_data *data = dev->data;

	if (data->current_pixel_format != PIXEL_FORMAT_I_4) {
		return NULL;
	}

	return display_get_framebuffer(config->display);
}

static void palette_dither_display_get_capabilities(const struct device *dev,
						    struct display_capabilities *caps)
{
	const struct palette_dither_display_config *config = dev->config;
	const struct palette_dither_display_data *data = dev->data;

	display_get_capabilities(config->display, caps);
	caps->supported_pixel_formats = PIXEL_FORMAT_I_4 |
					DISPLAY_COLOR_PALETTE_EMULATED_PIXEL_FORMATS;
	caps->current_pixel_format = data->current_pixel_format;
}

static int palette_dither_display_set_pixel_format(const struct device *dev,
						   const enum display_pixel_format pixel_format)
{
	struct palette_dither_display_data *data = dev->data;

	if (!palette_dither_display_pixel_format_supported(pixel_format)) {
		return -ENOTSUP;
	}

	data->current_pixel_format = pixel_format;
	return 0;
}

static int palette_dither_display_set_orientation(const struct device *dev,
						  const enum display_orientation orientation)
{
	const struct palette_dither_display_config *config = dev->config;

	return display_set_orientation(config->display, orientation);
}

static int palette_dither_display_init(const struct device *dev)
{
	const struct palette_dither_display_config *config = dev->config;
	struct palette_dither_display_data *data = dev->data;
	int ret;

	if (!device_is_ready(config->display)) {
		LOG_ERR("Wrapped display not ready");
		return -ENODEV;
	}

	if (!palette_dither_display_pixel_format_supported(config->initial_pixel_format)) {
		LOG_ERR("Unsupported initial pixel format");
		return -EINVAL;
	}

	ret = palette_dither_display_set_wrapped_pixel_format(config->display);
	if (ret < 0) {
		LOG_ERR("Wrapped display must support PIXEL_FORMAT_I_4: %d", ret);
		return ret;
	}

	data->current_pixel_format = config->initial_pixel_format;
	return 0;
}

static DEVICE_API(display, palette_dither_display_api) = {
	.blanking_on = palette_dither_display_blanking_on,
	.blanking_off = palette_dither_display_blanking_off,
	.write = palette_dither_display_write,
	.read = palette_dither_display_read,
	.clear = palette_dither_display_clear,
	.get_framebuffer = palette_dither_display_get_framebuffer,
	.get_capabilities = palette_dither_display_get_capabilities,
	.set_pixel_format = palette_dither_display_set_pixel_format,
	.set_orientation = palette_dither_display_set_orientation,
};

#define PALETTE_DITHER_DISPLAY_DEFINE(inst)                                                       \
	static uint8_t palette_dither_display_converted_buf_##inst\
		[DISPLAY_COLOR_PALETTE_I4_BUFFER_SIZE(                                    \
			DT_PROP(DT_INST_PHANDLE(inst, display), width),                       \
			DT_PROP(DT_INST_PHANDLE(inst, display), height))];                    \
	static const struct palette_dither_display_config palette_dither_display_config_##inst = { \
		.display = DEVICE_DT_GET(DT_INST_PHANDLE(inst, display)),                        \
		.initial_pixel_format = DT_INST_PROP(inst, pixel_format),                        \
		.converted_buf_size = ARRAY_SIZE(palette_dither_display_converted_buf_##inst),   \
	};                                                                                        \
	static struct palette_dither_display_data palette_dither_display_data_##inst = {          \
		.current_pixel_format = DT_INST_PROP(inst, pixel_format),                        \
		.converted_buf = palette_dither_display_converted_buf_##inst,                    \
	};                                                                                        \
	DEVICE_DT_INST_DEFINE(inst, palette_dither_display_init, NULL,                           \
			      &palette_dither_display_data_##inst,                               \
			      &palette_dither_display_config_##inst, POST_KERNEL,                \
			      CONFIG_DISPLAY_INIT_PRIORITY, &palette_dither_display_api);

DT_INST_FOREACH_STATUS_OKAY(PALETTE_DITHER_DISPLAY_DEFINE)
