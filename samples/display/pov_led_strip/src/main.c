/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <string.h>

LOG_MODULE_REGISTER(pov_sample, CONFIG_LOG_DEFAULT_LEVEL);

static void fill_pattern(uint8_t *buf, const struct display_capabilities *caps, uint32_t phase)
{
	const uint16_t width = caps->x_resolution;
	const uint16_t height = caps->y_resolution;
	const bool argb = (caps->current_pixel_format == PIXEL_FORMAT_ARGB_8888);
	const size_t bpp = argb ? 4U : 3U;

	for (uint16_t y = 0; y < height; y++) {
		for (uint16_t x = 0; x < width; x++) {
			const uint8_t r = (uint8_t)((x * 255U) / MAX(width, 1U));
			const uint8_t g = (uint8_t)(((y + phase) * 255U) / MAX(height, 1U));
			const uint8_t b = (uint8_t)(((x + phase) * 17U) & 0xFF);
			size_t idx = ((size_t)y * width + x) * bpp;

			if (argb) {
				uint32_t pixel = 0xFF000000U | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;

				memcpy(&buf[idx], &pixel, sizeof(pixel));
			} else {
				buf[idx] = r;
				buf[idx + 1] = g;
				buf[idx + 2] = b;
			}
		}
	}
}

int main(void)
{
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	struct display_capabilities caps;
	struct display_buffer_descriptor desc;
	uint8_t *frame;
	size_t bpp;
	uint32_t phase = 0U;
	int ret;

	if (!device_is_ready(display)) {
		LOG_ERR("Display device is not ready");
		return 0;
	}

	display_get_capabilities(display, &caps);

	bpp = (caps.current_pixel_format == PIXEL_FORMAT_ARGB_8888) ? 4U : 3U;
	memset(&desc, 0, sizeof(desc));
	desc.width = caps.x_resolution;
	desc.height = caps.y_resolution;
	desc.pitch = caps.x_resolution;
	desc.buf_size = (size_t)desc.width * desc.height * bpp;

	frame = k_malloc(desc.buf_size);
	if (frame == NULL) {
		LOG_ERR("Unable to allocate %zu-byte framebuffer", desc.buf_size);
		return 0;
	}

	ret = display_blanking_off(display);
	if (ret < 0) {
		LOG_WRN("Failed to disable blanking (%d)", ret);
	}

	LOG_INF("POV pattern frame generator started (%ux%u)",
		caps.x_resolution, caps.y_resolution);

	while (true) {
		fill_pattern(frame, &caps, phase);

		ret = display_write(display, 0, 0, &desc, frame);
		if (ret < 0) {
			LOG_ERR("display_write failed: %d", ret);
			break;
		}

		LOG_INF("POV pattern frame %u", phase);
		phase++;
		k_msleep(100);
	}

	k_free(frame);

	return 0;
}
