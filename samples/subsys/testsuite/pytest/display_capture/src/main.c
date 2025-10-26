/*
 * Copyright (c) 2025 Contributors to the Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sample, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

#ifdef CONFIG_ARCH_POSIX
#include "posix_board_if.h"
#endif

/**
 * @brief Simple display demo for showcasing the display_capture harness
 *
 * This sample demonstrates a basic display application that can be tested
 * using the display_capture harness with Twister. It draws colored rectangles
 * on the display and can be verified using video capture and fingerprinting.
 */

static void fill_buffer_rgb565(uint16_t color, uint8_t *buf, size_t buf_size)
{
	for (size_t idx = 0; idx < buf_size; idx += 2) {
		*(uint16_t *)(buf + idx) = color;
	}
}

static void fill_buffer_argb8888(uint32_t color, uint8_t *buf, size_t buf_size)
{
	for (size_t idx = 0; idx < buf_size; idx += 4) {
		*((uint32_t *)(buf + idx)) = color;
	}
}

int main(void)
{
	const struct device *display_dev;
	struct display_capabilities capabilities;
	struct display_buffer_descriptor buf_desc;
	size_t buf_size;
	uint8_t *buf;

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device %s not found. Aborting sample.", display_dev->name);
#ifdef CONFIG_ARCH_POSIX
		posix_exit(1);
#else
		return 0;
#endif
	}

	LOG_INF("Display sample for %s", display_dev->name);
	display_get_capabilities(display_dev, &capabilities);

	LOG_INF("Display resolution: %dx%d", capabilities.x_resolution,
		capabilities.y_resolution);

	/* Allocate a simple buffer for a rectangle */
	size_t rect_w = capabilities.x_resolution / 2;
	size_t rect_h = capabilities.y_resolution / 2;

	buf_size = rect_w * rect_h;

	/* Adjust buffer size based on pixel format */
	switch (capabilities.current_pixel_format) {
	case PIXEL_FORMAT_ARGB_8888:
		buf_size *= 4;
		break;
	case PIXEL_FORMAT_RGB_888:
		buf_size *= 3;
		break;
	case PIXEL_FORMAT_RGB_565:
	case PIXEL_FORMAT_BGR_565:
		buf_size *= 2;
		break;
	default:
		LOG_ERR("Unsupported pixel format. Aborting sample.");
#ifdef CONFIG_ARCH_POSIX
		posix_exit(1);
#else
		return 0;
#endif
	}

	buf = k_malloc(buf_size);
	if (buf == NULL) {
		LOG_ERR("Could not allocate memory. Aborting sample.");
#ifdef CONFIG_ARCH_POSIX
		posix_exit(1);
#else
		return 0;
#endif
	}

	buf_desc.buf_size = buf_size;
	buf_desc.pitch = rect_w;
	buf_desc.width = rect_w;
	buf_desc.height = rect_h;
	buf_desc.frame_incomplete = false;

	/* Draw a red rectangle in the top-left */
	if (capabilities.current_pixel_format == PIXEL_FORMAT_RGB_565 ||
	    capabilities.current_pixel_format == PIXEL_FORMAT_BGR_565) {
		fill_buffer_rgb565(0xF800, buf, buf_size); /* Red in RGB565 */
	} else if (capabilities.current_pixel_format == PIXEL_FORMAT_ARGB_8888) {
		fill_buffer_argb8888(0xFFFF0000, buf, buf_size); /* Red in ARGB8888 */
	}

	display_write(display_dev, 0, 0, &buf_desc, buf);

	/* Turn on the display */
	display_blanking_off(display_dev);

	LOG_INF("Display starts");

	/* Keep the display on */
	while (1) {
		k_sleep(K_SECONDS(1));
	}

#ifdef CONFIG_ARCH_POSIX
	posix_exit(0);
#endif
	return 0;
}
