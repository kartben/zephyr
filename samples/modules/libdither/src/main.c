/*
 * Copyright (c) 2026 Anthropic
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "libdither.h"

LOG_MODULE_REGISTER(libdither_sample, LOG_LEVEL_INF);

/*
 * Render a dithered 1-bit image (one byte per pixel, 0x00=black/0xFF=white)
 * to the display, handling each Zephyr pixel format.
 */
static void render_dithered(const struct device *dev,
			     const struct display_capabilities *caps,
			     const uint8_t *dithered)
{
	const int width = caps->x_resolution;
	const int height = caps->y_resolution;
	struct display_buffer_descriptor desc = {
		.width = width,
		.pitch = width,
	};

	if (caps->screen_info & SCREEN_INFO_MONO_VTILED) {
		/*
		 * Monochrome vertically-tiled displays (e.g. SSD1306):
		 * each byte in the buffer represents 8 vertically adjacent
		 * pixels for one column; bit 0 = top pixel of the tile.
		 * MONO01: 1 = white.  MONO10: 1 = black.
		 */
		uint8_t *buf = k_malloc(width);

		if (!buf) {
			LOG_ERR("Out of memory for display buffer");
			return;
		}

		for (int y = 0; y < height; y += 8) {
			int tile_h = MIN(8, height - y);

			for (int x = 0; x < width; x++) {
				uint8_t byte = 0;

				for (int r = 0; r < tile_h; r++) {
					bool white = dithered[(y + r) * width + x] >= 0x80;

					if (caps->current_pixel_format == PIXEL_FORMAT_MONO01
					    ? white : !white) {
						byte |= BIT(r);
					}
				}
				buf[x] = byte;
			}

			desc.buf_size = width;
			desc.height = tile_h;
			desc.frame_incomplete = (y + 8 < height);
			display_write(dev, 0, y, &desc, buf);
		}

		k_free(buf);
		return;
	}

	/* Row-based pixel formats — determine bytes per pixel. */
	size_t bpp;

	switch (caps->current_pixel_format) {
	case PIXEL_FORMAT_L_8:
		bpp = 1;
		break;
	case PIXEL_FORMAT_RGB_565:
	case PIXEL_FORMAT_RGB_565X:
	case PIXEL_FORMAT_AL_88:
		bpp = 2;
		break;
	case PIXEL_FORMAT_RGB_888:
	case PIXEL_FORMAT_BGR_888:
		bpp = 3;
		break;
	default: /* ARGB/RGBA/BGRA/ABGR 8888 and XRGB 8888 */
		bpp = 4;
		break;
	}

	const int STRIP_H = 16;
	uint8_t *buf = k_malloc(width * STRIP_H * bpp);

	if (!buf) {
		LOG_ERR("Out of memory for display buffer");
		return;
	}

	for (int y = 0; y < height; y += STRIP_H) {
		int cur_h = MIN(STRIP_H, height - y);
		uint8_t *p = buf;

		for (int row = 0; row < cur_h; row++) {
			for (int x = 0; x < width; x++) {
				uint8_t v = (dithered[(y + row) * width + x] >= 0x80)
					    ? 0xFF : 0x00;

				switch (caps->current_pixel_format) {
				case PIXEL_FORMAT_L_8:
					*p++ = v;
					break;
				case PIXEL_FORMAT_RGB_565:
				case PIXEL_FORMAT_RGB_565X:
					*p++ = v;
					*p++ = v;
					break;
				case PIXEL_FORMAT_RGB_888:
				case PIXEL_FORMAT_BGR_888:
					*p++ = v;
					*p++ = v;
					*p++ = v;
					break;
				case PIXEL_FORMAT_ARGB_8888:
				case PIXEL_FORMAT_XRGB_8888:
					*p++ = 0xFF; /* alpha / padding */
					*p++ = v;    /* R */
					*p++ = v;    /* G */
					*p++ = v;    /* B */
					break;
				case PIXEL_FORMAT_RGBA_8888:
					*p++ = v;    /* R */
					*p++ = v;    /* G */
					*p++ = v;    /* B */
					*p++ = 0xFF; /* alpha */
					break;
				case PIXEL_FORMAT_BGRA_8888:
					*p++ = v;    /* B */
					*p++ = v;    /* G */
					*p++ = v;    /* R */
					*p++ = 0xFF; /* alpha */
					break;
				case PIXEL_FORMAT_ABGR_8888:
					*p++ = 0xFF; /* alpha */
					*p++ = v;    /* B */
					*p++ = v;    /* G */
					*p++ = v;    /* R */
					break;
				case PIXEL_FORMAT_AL_88:
					*p++ = 0xFF; /* alpha */
					*p++ = v;    /* luminance */
					break;
				default:
					*p++ = v;
					break;
				}
			}
		}

		desc.buf_size = width * cur_h * bpp;
		desc.height = cur_h;
		desc.frame_incomplete = (y + STRIP_H < height);
		display_write(dev, 0, y, &desc, buf);
	}

	k_free(buf);
}

int main(void)
{
	const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device not ready");
		return -ENODEV;
	}

	struct display_capabilities caps;

	display_get_capabilities(display_dev, &caps);

	const int width = caps.x_resolution;
	const int height = caps.y_resolution;

	LOG_INF("Display: %dx%d", width, height);

	/* Build a horizontal gradient image: black (left) -> white (right). */
	DitherImage *img = DitherImage_new(width, height);

	if (!img) {
		LOG_ERR("Failed to allocate DitherImage");
		return -ENOMEM;
	}

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int v = (x * 255) / MAX(width - 1, 1);

			DitherImage_set_pixel(img, x, y, v, v, v, false);
		}
	}

	uint8_t *dithered = k_malloc(width * height);

	if (!dithered) {
		LOG_ERR("Failed to allocate output buffer");
		DitherImage_free(img);
		return -ENOMEM;
	}

	display_blanking_off(display_dev);

	bool first_cycle = true;

	while (1) {
		/* --- Floyd-Steinberg error diffusion --- */
		LOG_INF("Applying Floyd-Steinberg dithering...");
		ErrorDiffusionMatrix *fs = get_floyd_steinberg_matrix();

		error_diffusion_dither(img, fs, true, 0.0, dithered);
		ErrorDiffusionMatrix_free(fs);
		render_dithered(display_dev, &caps, dithered);
		k_msleep(2000);

		/* --- Atkinson dithering --- */
		LOG_INF("Applying Atkinson dithering...");
		ErrorDiffusionMatrix *at = get_atkinson_matrix();

		error_diffusion_dither(img, at, true, 0.0, dithered);
		ErrorDiffusionMatrix_free(at);
		render_dithered(display_dev, &caps, dithered);
		k_msleep(2000);

		/* --- Bayer 4x4 ordered dithering --- */
		LOG_INF("Applying Bayer 4x4 ordered dithering...");
		OrderedDitherMatrix *b4 = get_bayer4x4_matrix();

		ordered_dither(img, b4, 0.0, dithered);
		OrderedDitherMatrix_free(b4);
		render_dithered(display_dev, &caps, dithered);
		k_msleep(2000);

		/* --- Bayer 8x8 ordered dithering --- */
		LOG_INF("Applying Bayer 8x8 ordered dithering...");
		OrderedDitherMatrix *b8 = get_bayer8x8_matrix();

		ordered_dither(img, b8, 0.0, dithered);
		OrderedDitherMatrix_free(b8);
		render_dithered(display_dev, &caps, dithered);
		k_msleep(2000);

		if (first_cycle) {
			LOG_INF("libdither sample done");
			first_cycle = false;
		}
	}

	return 0;
}
