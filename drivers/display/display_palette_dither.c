/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_palette_dither_display

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(display_palette_dither, CONFIG_DISPLAY_LOG_LEVEL);

#define PALETTE_DITHER_DITHER_STEP  8
#define PALETTE_DITHER_BAYER_OFFSET 8
#define PALETTE_DITHER_EMULATED_PIXEL_FORMATS                                                      \
	(PIXEL_FORMAT_RGB_565 | PIXEL_FORMAT_RGB_888)
#define PALETTE_DITHER_SUPPORTED_PIXEL_FORMATS                                                     \
	(PIXEL_FORMAT_I_4 | PALETTE_DITHER_EMULATED_PIXEL_FORMATS)
#define PALETTE_DITHER_I4_BUFFER_SIZE(width, height) (DIV_ROUND_UP((width), 2U) * (height))
#define PALETTE_DITHER_ALGORITHM_IDX_NONE              0
#define PALETTE_DITHER_ALGORITHM_IDX_ORDERED_BAYER_4X4 1
#define PALETTE_DITHER_ALGORITHM_IDX_ORDERED_BAYER_8X8 2
#define PALETTE_DITHER_ALGORITHM_IDX_FLOYD_STEINBERG   3

enum palette_dither_algorithm {
	PALETTE_DITHER_ALGORITHM_NONE = PALETTE_DITHER_ALGORITHM_IDX_NONE,
	PALETTE_DITHER_ALGORITHM_ORDERED_BAYER_4X4 = PALETTE_DITHER_ALGORITHM_IDX_ORDERED_BAYER_4X4,
	PALETTE_DITHER_ALGORITHM_ORDERED_BAYER_8X8 = PALETTE_DITHER_ALGORITHM_IDX_ORDERED_BAYER_8X8,
	PALETTE_DITHER_ALGORITHM_FLOYD_STEINBERG = PALETTE_DITHER_ALGORITHM_IDX_FLOYD_STEINBERG,
};

/*
 * Standard 4x4 Bayer ordered-dithering matrix. The 8-point centering offset
 * makes the matrix symmetric around zero, and a step of 8 keeps the ordered
 * dither visible enough to mix palette entries without overwhelming the source
 * image on common small display palettes.
 */
static const uint8_t palette_dither_bayer4x4[4][4] = {
	{0, 8, 2, 10},
	{12, 4, 14, 6},
	{3, 11, 1, 9},
	{15, 7, 13, 5},
};

/*
 * Standard 8x8 Bayer matrix. A 2-point step gives it a similar component range
 * to the 4x4 matrix while using finer spatial thresholds.
 */
static const uint8_t palette_dither_bayer8x8[8][8] = {
	{0, 48, 12, 60, 3, 51, 15, 63}, {32, 16, 44, 28, 35, 19, 47, 31},
	{8, 56, 4, 52, 11, 59, 7, 55},  {40, 24, 36, 20, 43, 27, 39, 23},
	{2, 50, 14, 62, 1, 49, 13, 61}, {34, 18, 46, 30, 33, 17, 45, 29},
	{10, 58, 6, 54, 9, 57, 5, 53},  {42, 26, 38, 22, 41, 25, 37, 21},
};

struct palette_dither_rgb_error {
	int16_t r;
	int16_t g;
	int16_t b;
};

struct palette_dither_display_config {
	const struct device *display;
	enum display_pixel_format initial_pixel_format;
	enum palette_dither_algorithm dithering_algorithm;
	size_t converted_buf_size;
};

struct palette_dither_display_data {
	enum display_pixel_format current_pixel_format;
	uint8_t *converted_buf;
	struct palette_dither_rgb_error *fs_current_error;
	struct palette_dither_rgb_error *fs_next_error;
};

static uint8_t palette_dither_clamp_component(int16_t component)
{
	return CLAMP(component, 0, UINT8_MAX);
}

static uint8_t palette_dither_ordered_component(uint8_t component, uint8_t threshold,
						uint8_t threshold_offset, uint8_t step)
{
	int16_t offset;

	offset = ((int16_t)threshold - (int16_t)threshold_offset) * step;

	return palette_dither_clamp_component((int16_t)component + offset);
}

static void palette_dither_ordered_rgb(enum palette_dither_algorithm algorithm, uint32_t x,
				       uint32_t y, uint8_t *r, uint8_t *g, uint8_t *b)
{
	uint8_t threshold;
	uint8_t offset;
	uint8_t step;

	switch (algorithm) {
	case PALETTE_DITHER_ALGORITHM_ORDERED_BAYER_4X4:
		threshold = palette_dither_bayer4x4[y & 0x3U][x & 0x3U];
		offset = PALETTE_DITHER_BAYER_OFFSET;
		step = PALETTE_DITHER_DITHER_STEP;
		break;
	case PALETTE_DITHER_ALGORITHM_ORDERED_BAYER_8X8:
		threshold = palette_dither_bayer8x8[y & 0x7U][x & 0x7U];
		offset = 32U;
		step = 2U;
		break;
	default:
		return;
	}

	*r = palette_dither_ordered_component(*r, threshold, offset, step);
	*g = palette_dither_ordered_component(*g, threshold, offset, step);
	*b = palette_dither_ordered_component(*b, threshold, offset, step);
}

static int palette_dither_decode_rgb(const struct display_buffer_descriptor *desc,
				     enum display_pixel_format pixel_format, const void *buf,
				     uint32_t x, uint32_t y, uint8_t *r, uint8_t *g,
				     uint8_t *b)
{
	uint32_t pixel_idx = y * desc->pitch + x;
	const uint8_t *buf8 = buf;

	switch (pixel_format) {
	case PIXEL_FORMAT_RGB_565: {
		uint16_t pixel = sys_le16_to_cpu(((const uint16_t *)buf)[pixel_idx]);

		*r = ((pixel >> 11) & 0x1FU) * 255U / 31U;
		*g = ((pixel >> 5) & 0x3FU) * 255U / 63U;
		*b = (pixel & 0x1FU) * 255U / 31U;
		return 0;
	}
	case PIXEL_FORMAT_RGB_888:
		buf8 += pixel_idx * 3U;
		*r = buf8[0];
		*g = buf8[1];
		*b = buf8[2];
		return 0;
	default:
		return -ENOTSUP;
	}
}

static size_t palette_dither_output_buffer_size(const struct display_buffer_descriptor *desc,
						enum display_pixel_format pixel_format)
{
	return (desc->pitch * desc->height * DISPLAY_BITS_PER_PIXEL(pixel_format)) / 8U;
}

static bool palette_dither_color_is_null(const struct display_palette_color *color)
{
	return (color->r == 0U) && (color->g == 0U) && (color->b == 0U) && (color->a == 0U);
}

static int palette_dither_find_nearest_index(const struct display_palette_color *palette,
					     size_t palette_len, uint8_t r, uint8_t g,
					     uint8_t b)
{
	uint32_t best_distance = UINT32_MAX;
	int best_index = -1;

	for (size_t i = 0; i < palette_len; i++) {
		int16_t dr;
		int16_t dg;
		int16_t db;
		uint32_t distance;

		if (palette_dither_color_is_null(&palette[i])) {
			continue;
		}

		dr = (int16_t)r - (int16_t)palette[i].r;
		dg = (int16_t)g - (int16_t)palette[i].g;
		db = (int16_t)b - (int16_t)palette[i].b;
		distance = (uint32_t)(dr * dr) + (uint32_t)(dg * dg) + (uint32_t)(db * db);

		if (distance < best_distance) {
			best_distance = distance;
			best_index = (int)i;
			if (distance == 0U) {
				break;
			}
		}
	}

	return best_index;
}

static void palette_dither_write_i4_index(uint8_t *converted_buf, size_t row_size, uint32_t x,
					  uint32_t y, uint8_t palette_idx)
{
	size_t byte_idx = (y * row_size) + (x / 2U);

	if ((x & 0x1U) == 0U) {
		converted_buf[byte_idx] = palette_idx << 4;
	} else {
		converted_buf[byte_idx] |= palette_idx & 0x0FU;
	}
}

static int palette_dither_write_nearest_i4(const struct display_palette_color *palette,
					   size_t palette_len, uint8_t r, uint8_t g, uint8_t b,
					   uint8_t *converted_buf, size_t row_size, uint32_t x,
					   uint32_t y)
{
	int palette_idx;

	palette_idx = palette_dither_find_nearest_index(palette, palette_len, r, g, b);
	if (palette_idx < 0) {
		return -ENOTSUP;
	}

	palette_dither_write_i4_index(converted_buf, row_size, x, y, (uint8_t)palette_idx);

	return palette_idx;
}

static void palette_dither_add_error(struct palette_dither_rgb_error *error, int16_t r, int16_t g,
				     int16_t b, int16_t scale)
{
	error->r = CLAMP((int32_t)error->r + (r * scale), INT16_MIN, INT16_MAX);
	error->g = CLAMP((int32_t)error->g + (g * scale), INT16_MIN, INT16_MAX);
	error->b = CLAMP((int32_t)error->b + (b * scale), INT16_MIN, INT16_MAX);
}

static uint8_t palette_dither_apply_error(uint8_t component, int16_t error)
{
	return palette_dither_clamp_component((int16_t)component + (error / 16));
}

static int palette_dither_convert_to_i4_ordered(const struct display_buffer_descriptor *desc,
						enum display_pixel_format pixel_format,
						const void *buf,
						const struct display_palette_color *palette,
						size_t palette_len, uint8_t *converted_buf,
						enum palette_dither_algorithm algorithm)
{
	size_t row_size = DIV_ROUND_UP(desc->width, 2U);

	for (uint32_t y = 0U; y < desc->height; y++) {
		for (uint32_t x = 0U; x < desc->width; x++) {
			uint8_t r;
			uint8_t g;
			uint8_t b;

			if (palette_dither_decode_rgb(desc, pixel_format, buf, x, y, &r, &g, &b) !=
			    0) {
				return -ENOTSUP;
			}

			palette_dither_ordered_rgb(algorithm, x, y, &r, &g, &b);
			if (palette_dither_write_nearest_i4(palette, palette_len, r, g, b,
							    converted_buf, row_size, x, y) < 0) {
				return -ENOTSUP;
			}
		}
	}

	return 0;
}

static int palette_dither_convert_to_i4_floyd_steinberg(
	const struct display_buffer_descriptor *desc, enum display_pixel_format pixel_format,
	const void *buf, const struct display_palette_color *palette, size_t palette_len,
	uint8_t *converted_buf, struct palette_dither_rgb_error *current_error,
	struct palette_dither_rgb_error *next_error)
{
	size_t error_row_size = desc->width + 1U;
	size_t row_size = DIV_ROUND_UP(desc->width, 2U);
	struct palette_dither_rgb_error *tmp_error;

	if ((current_error == NULL) || (next_error == NULL)) {
		return -EINVAL;
	}

	memset(current_error, 0, error_row_size * sizeof(*current_error));

	for (uint32_t y = 0U; y < desc->height; y++) {
		memset(next_error, 0, error_row_size * sizeof(*next_error));

		for (uint32_t x = 0U; x < desc->width; x++) {
			const struct display_palette_color *color;
			int16_t error_r;
			int16_t error_g;
			int16_t error_b;
			uint8_t r;
			uint8_t g;
			uint8_t b;
			int palette_idx;

			if (palette_dither_decode_rgb(desc, pixel_format, buf, x, y, &r, &g, &b) !=
			    0) {
				return -ENOTSUP;
			}

			r = palette_dither_apply_error(r, current_error[x].r);
			g = palette_dither_apply_error(g, current_error[x].g);
			b = palette_dither_apply_error(b, current_error[x].b);
			palette_idx = palette_dither_write_nearest_i4(
				palette, palette_len, r, g, b, converted_buf, row_size, x, y);
			if (palette_idx < 0) {
				return -ENOTSUP;
			}

			color = &palette[palette_idx];
			error_r = (int16_t)r - (int16_t)color->r;
			error_g = (int16_t)g - (int16_t)color->g;
			error_b = (int16_t)b - (int16_t)color->b;

			if (x + 1U < desc->width) {
				palette_dither_add_error(&current_error[x + 1U], error_r, error_g,
							 error_b, 7);
			}

			if (y + 1U >= desc->height) {
				continue;
			}

			if (x > 0U) {
				palette_dither_add_error(&next_error[x - 1U], error_r, error_g,
							 error_b, 3);
			}

			palette_dither_add_error(&next_error[x], error_r, error_g, error_b, 5);

			if (x + 1U < desc->width) {
				palette_dither_add_error(&next_error[x + 1U], error_r, error_g,
							 error_b, 1);
			}
		}

		tmp_error = current_error;
		current_error = next_error;
		next_error = tmp_error;
	}

	return 0;
}

static int palette_dither_convert_to_i4(const struct display_buffer_descriptor *desc,
					enum display_pixel_format pixel_format, const void *buf,
					const struct display_palette_color *palette,
					size_t palette_len, uint8_t *converted_buf,
					size_t converted_buf_size,
					enum palette_dither_algorithm algorithm,
					struct palette_dither_rgb_error *fs_current_error,
					struct palette_dither_rgb_error *fs_next_error)
{
	size_t required_size;

	if ((desc == NULL) || (buf == NULL) || (palette == NULL) || (converted_buf == NULL)) {
		return -EINVAL;
	}

	if (desc->width > desc->pitch) {
		return -EINVAL;
	}

	required_size = PALETTE_DITHER_I4_BUFFER_SIZE(desc->width, desc->height);
	if (converted_buf_size < required_size) {
		return -EINVAL;
	}

	if (DISPLAY_BITS_PER_PIXEL(pixel_format) == 0U) {
		return -ENOTSUP;
	}

	if (desc->buf_size <
	    (desc->pitch * desc->height * DISPLAY_BITS_PER_PIXEL(pixel_format)) / 8U) {
		return -EINVAL;
	}

	memset(converted_buf, 0, required_size);

	switch (algorithm) {
	case PALETTE_DITHER_ALGORITHM_NONE:
	case PALETTE_DITHER_ALGORITHM_ORDERED_BAYER_4X4:
	case PALETTE_DITHER_ALGORITHM_ORDERED_BAYER_8X8:
		return palette_dither_convert_to_i4_ordered(desc, pixel_format, buf, palette,
							    palette_len, converted_buf, algorithm);
	case PALETTE_DITHER_ALGORITHM_FLOYD_STEINBERG:
		return palette_dither_convert_to_i4_floyd_steinberg(
			desc, pixel_format, buf, palette, palette_len, converted_buf,
			fs_current_error, fs_next_error);
	default:
		return -ENOTSUP;
	}
}

static int palette_dither_convert_from_i4(const struct display_buffer_descriptor *desc,
					  enum display_pixel_format pixel_format,
					  const uint8_t *buf,
					  const struct display_palette_color *palette,
					  size_t palette_len, void *converted_buf,
					  size_t converted_buf_size)
{
	size_t input_row_size;
	size_t required_size;

	if ((desc == NULL) || (buf == NULL) || (palette == NULL) || (converted_buf == NULL)) {
		return -EINVAL;
	}

	if (desc->width > desc->pitch) {
		return -EINVAL;
	}

	required_size = palette_dither_output_buffer_size(desc, pixel_format);
	if (converted_buf_size < required_size) {
		return -EINVAL;
	}

	input_row_size = DIV_ROUND_UP(desc->pitch, 2U);
	if (PALETTE_DITHER_I4_BUFFER_SIZE(desc->pitch, desc->height) > desc->buf_size) {
		return -EINVAL;
	}

	for (uint32_t y = 0U; y < desc->height; y++) {
		for (uint32_t x = 0U; x < desc->width; x++) {
			uint8_t palette_idx;
			const struct display_palette_color *color;

			palette_idx = buf[(y * input_row_size) + (x / 2U)];
			palette_idx =
				((x & 0x1U) == 0U) ? (palette_idx >> 4) : (palette_idx & 0x0FU);
			if (palette_idx >= palette_len) {
				return -EINVAL;
			}

			color = &palette[palette_idx];

			switch (pixel_format) {
			case PIXEL_FORMAT_RGB_565: {
				uint16_t pixel = (((uint16_t)color->r >> 3) << 11) |
						 (((uint16_t)color->g >> 2) << 5) |
						 ((uint16_t)color->b >> 3);

				((uint16_t *)converted_buf)[(y * desc->pitch) + x] =
					sys_cpu_to_le16(pixel);
				break;
			}
			case PIXEL_FORMAT_RGB_888: {
				uint8_t *dst =
					(uint8_t *)converted_buf + (((y * desc->pitch) + x) * 3U);

				dst[0] = color->r;
				dst[1] = color->g;
				dst[2] = color->b;
				break;
			}
			default:
				return -ENOTSUP;
			}
		}
	}

	return 0;
}

static bool palette_dither_display_pixel_format_supported(enum display_pixel_format pixel_format)
{
	return IS_POWER_OF_TWO(pixel_format) &&
	       ((pixel_format & PALETTE_DITHER_SUPPORTED_PIXEL_FORMATS) == pixel_format);
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
	size_t converted_size;
	int ret;

	if (data->current_pixel_format == PIXEL_FORMAT_I_4) {
		// passthrough to the wrapped display, no dithering needed
		return display_write(config->display, x, y, desc, buf);
	}

	converted_size = PALETTE_DITHER_I4_BUFFER_SIZE(desc->width, desc->height);
	if (converted_size > config->converted_buf_size) {
		return -EINVAL;
	}

	display_get_capabilities(config->display, &caps);
	ret = palette_dither_convert_to_i4(
		desc, data->current_pixel_format, buf, caps.color_palette,
		ARRAY_SIZE(caps.color_palette), data->converted_buf, converted_size,
		config->dithering_algorithm, data->fs_current_error, data->fs_next_error);
	if (ret < 0) {
		return ret;
	}

	i4_desc = *desc;
	i4_desc.buf_size = converted_size;
	i4_desc.pitch = desc->width;

	return display_write(config->display, x, y, &i4_desc, data->converted_buf);
}

static int palette_dither_display_read(const struct device *dev, const uint16_t x, const uint16_t y,
				       const struct display_buffer_descriptor *desc, void *buf)
{
	const struct palette_dither_display_config *config = dev->config;
	struct palette_dither_display_data *data = dev->data;
	struct display_capabilities caps;
	struct display_buffer_descriptor i4_desc;
	size_t converted_size;
	int ret;

	if (data->current_pixel_format == PIXEL_FORMAT_I_4) {
		return display_read(config->display, x, y, desc, buf);
	}

	converted_size = PALETTE_DITHER_I4_BUFFER_SIZE(desc->width, desc->height);
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
	return palette_dither_convert_from_i4(&i4_desc, data->current_pixel_format,
					      data->converted_buf, caps.color_palette,
					      ARRAY_SIZE(caps.color_palette), buf, desc->buf_size);
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
	caps->supported_pixel_formats = PALETTE_DITHER_SUPPORTED_PIXEL_FORMATS;
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

#define PALETTE_DITHER_DISPLAY_BUF_SIZE(inst)                                                      \
	PALETTE_DITHER_I4_BUFFER_SIZE(DT_INST_PROP(inst, width), DT_INST_PROP(inst, height))

#define PALETTE_DITHER_DISPLAY_ERROR_BUF_SIZE(inst) (DT_INST_PROP(inst, width) + 1U)

#define PALETTE_DITHER_DISPLAY_ALGORITHM(inst)                                                     \
	DT_INST_ENUM_IDX_OR(inst, dithering_algorithm,                                             \
			    PALETTE_DITHER_ALGORITHM_IDX_ORDERED_BAYER_4X4)

#define PALETTE_DITHER_FS_ERROR_BUF_DEFINE(inst)                                                   \
	COND_CODE_1(IS_EQ(PALETTE_DITHER_DISPLAY_ALGORITHM(inst),                                 \
			  PALETTE_DITHER_ALGORITHM_IDX_FLOYD_STEINBERG),                          \
		    (static struct palette_dither_rgb_error palette_dither_fs_current_error_##inst \
			     [PALETTE_DITHER_DISPLAY_ERROR_BUF_SIZE(inst)];                         \
		     static struct palette_dither_rgb_error palette_dither_fs_next_error_##inst     \
			     [PALETTE_DITHER_DISPLAY_ERROR_BUF_SIZE(inst)];),                        \
		    ())

#define PALETTE_DITHER_FS_ERROR_BUF_PTR(inst, name)                                                \
	COND_CODE_1(IS_EQ(PALETTE_DITHER_DISPLAY_ALGORITHM(inst),                                 \
			  PALETTE_DITHER_ALGORITHM_IDX_FLOYD_STEINBERG),                          \
		    (palette_dither_fs_##name##_error_##inst), (NULL))

#define PALETTE_DITHER_DISPLAY_DEFINE(inst)                                                        \
	static uint8_t palette_dither_buf_##inst[PALETTE_DITHER_DISPLAY_BUF_SIZE(inst)];           \
	PALETTE_DITHER_FS_ERROR_BUF_DEFINE(inst)                                                   \
	static const struct palette_dither_display_config palette_dither_display_config_##inst = { \
		.display = DEVICE_DT_GET(DT_INST_PHANDLE(inst, display)),                          \
		.initial_pixel_format = DT_INST_PROP(inst, pixel_format),                          \
		.dithering_algorithm = PALETTE_DITHER_DISPLAY_ALGORITHM(inst),                     \
		.converted_buf_size = ARRAY_SIZE(palette_dither_buf_##inst),                       \
	};                                                                                         \
	static struct palette_dither_display_data palette_dither_display_data_##inst = {           \
		.current_pixel_format = DT_INST_PROP(inst, pixel_format),                          \
		.converted_buf = palette_dither_buf_##inst,                                        \
		.fs_current_error = PALETTE_DITHER_FS_ERROR_BUF_PTR(inst, current),                \
		.fs_next_error = PALETTE_DITHER_FS_ERROR_BUF_PTR(inst, next),                      \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, palette_dither_display_init, NULL,                             \
			      &palette_dither_display_data_##inst,                                 \
			      &palette_dither_display_config_##inst, POST_KERNEL,                  \
			      CONFIG_DISPLAY_INIT_PRIORITY, &palette_dither_display_api);

DT_INST_FOREACH_STATUS_OKAY(PALETTE_DITHER_DISPLAY_DEFINE)
