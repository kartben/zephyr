/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT led_strip_pov

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <string.h>

LOG_MODULE_REGISTER(led_strip_pov, CONFIG_DISPLAY_LOG_LEVEL);

struct led_strip_pov_config {
	const struct device *strip;
	const struct device *tach;
	uint16_t width;
	uint16_t height;
	enum display_pixel_format pixel_format;
	uint32_t default_rpm;
	uint32_t rpm_timeout_ms;
	uint32_t sensor_poll_ms;
	k_thread_stack_t *sensor_stack;
	size_t sensor_stack_size;
};

struct led_strip_pov_data {
	struct k_mutex fb_lock;
	struct led_rgb *framebuffer;
	struct led_rgb *column_buf;
	struct k_timer column_timer;
	struct k_work column_work;
	struct k_thread sensor_thread;
	bool blanked;
	uint16_t current_column;
	uint32_t column_interval_us;
	uint32_t active_rpm;
	const struct device *dev;
};

static inline size_t led_strip_pov_index(const struct led_strip_pov_config *config,
				       uint16_t x, uint16_t y)
{
	return ((size_t)y * config->width) + x;
}

static int led_strip_pov_check_descriptor(const struct led_strip_pov_config *config,
				       uint16_t x, uint16_t y,
				       const struct display_buffer_descriptor *desc,
				       size_t bytes_per_pixel)
{
	if (desc->width == 0U || desc->height == 0U) {
		return -EINVAL;
	}

	if (desc->width > desc->pitch ||
	    (x + desc->pitch) > config->width ||
	    (y + desc->height) > config->height) {
		return -EINVAL;
	}

	if (desc->buf_size < desc->pitch * desc->height * bytes_per_pixel) {
		return -EINVAL;
	}

	return 0;
}

static void led_strip_pov_update_timer(struct led_strip_pov_data *data,
				 uint32_t interval_us)
{
	if (interval_us == 0U) {
		interval_us = 1U;
	}

	data->column_interval_us = interval_us;

	k_timer_stop(&data->column_timer);

	if (!data->blanked) {
		k_timer_start(&data->column_timer, K_NO_WAIT, K_USEC(interval_us));
	}

}

static void led_strip_pov_push_column(struct led_strip_pov_data *data,
			     const struct led_strip_pov_config *config)
{
	uint16_t column = data->current_column;
	int ret;

	k_mutex_lock(&data->fb_lock, K_FOREVER);

	for (uint16_t row = 0; row < config->height; row++) {
		size_t idx = led_strip_pov_index(config, column, row);

		data->column_buf[row] = data->framebuffer[idx];
	}

	data->current_column = (column + 1U) % config->width;

	k_mutex_unlock(&data->fb_lock);

	ret = led_strip_update_rgb(config->strip, data->column_buf, config->height);
	if (ret < 0) {
		LOG_ERR("strip update failed: %d", ret);
	}
}

static void led_strip_pov_column_work(struct k_work *work)
{
	struct led_strip_pov_data *data = CONTAINER_OF(work, struct led_strip_pov_data, column_work);
	const struct led_strip_pov_config *config = data->dev->config;

	if (data->blanked) {
		return;
	}

	led_strip_pov_push_column(data, config);
}

static void led_strip_pov_timer_handler(struct k_timer *timer)
{
	struct led_strip_pov_data *data = CONTAINER_OF(timer, struct led_strip_pov_data, column_timer);

	k_work_submit(&data->column_work);
}

static void led_strip_pov_apply_blank(struct led_strip_pov_data *data,
			       const struct led_strip_pov_config *config,
			       bool blank)
{
	if (blank == data->blanked) {
		return;
	}

	data->blanked = blank;

	if (blank) {
		k_timer_stop(&data->column_timer);
		memset(data->column_buf, 0, sizeof(struct led_rgb) * config->height);
		(void)led_strip_update_rgb(config->strip, data->column_buf, config->height);
	} else {
		k_timer_start(&data->column_timer, K_NO_WAIT, K_USEC(data->column_interval_us));
	}
}

static int led_strip_pov_write(const struct device *dev, uint16_t x, uint16_t y,
			    const struct display_buffer_descriptor *desc,
			    const void *buf)
{
	struct led_strip_pov_data *data = dev->data;
	const struct led_strip_pov_config *config = dev->config;
	const uint8_t *src = buf;
	size_t bytes_per_pixel =
		(config->pixel_format == PIXEL_FORMAT_ARGB_8888) ? 4U : 3U;
	int ret;

	if (x >= config->width || y >= config->height) {
		return -EINVAL;
	}

	ret = led_strip_pov_check_descriptor(config, x, y, desc, bytes_per_pixel);
	if (ret < 0) {
		return ret;
	}

	k_mutex_lock(&data->fb_lock, K_FOREVER);

	for (uint16_t row = 0; row < desc->height; row++) {
		for (uint16_t col = 0; col < desc->width; col++) {
			size_t idx = led_strip_pov_index(config, x + col, y + row);
			struct led_rgb *pixel = &data->framebuffer[idx];

			if (config->pixel_format == PIXEL_FORMAT_ARGB_8888) {
				uint32_t color;

				memcpy(&color, src, sizeof(color));
				src += sizeof(color);

				pixel->r = (color >> 16) & 0xFF;
				pixel->g = (color >> 8) & 0xFF;
				pixel->b = color & 0xFF;
			} else {
				pixel->r = *src++;
				pixel->g = *src++;
				pixel->b = *src++;
			}
		}

		src += (desc->pitch - desc->width) * bytes_per_pixel;
	}

	k_mutex_unlock(&data->fb_lock);

	return 0;
}

static int led_strip_pov_read(const struct device *dev, uint16_t x, uint16_t y,
			   const struct display_buffer_descriptor *desc, void *buf)
{
	struct led_strip_pov_data *data = dev->data;
	const struct led_strip_pov_config *config = dev->config;
	uint8_t *dst = buf;
	size_t bytes_per_pixel =
		(config->pixel_format == PIXEL_FORMAT_ARGB_8888) ? 4U : 3U;
	int ret;

	if (x >= config->width || y >= config->height) {
		return -EINVAL;
	}

	ret = led_strip_pov_check_descriptor(config, x, y, desc, bytes_per_pixel);
	if (ret < 0) {
		return ret;
	}

	k_mutex_lock(&data->fb_lock, K_FOREVER);

	for (uint16_t row = 0; row < desc->height; row++) {
		for (uint16_t col = 0; col < desc->width; col++) {
			size_t idx = led_strip_pov_index(config, x + col, y + row);
			const struct led_rgb *pixel = &data->framebuffer[idx];

			if (config->pixel_format == PIXEL_FORMAT_ARGB_8888) {
				uint32_t color = 0xFF000000U |
				((uint32_t)pixel->r << 16) |
				((uint32_t)pixel->g << 8) |
				pixel->b;

				memcpy(dst, &color, sizeof(color));
				dst += sizeof(color);
			} else {
				*dst++ = pixel->r;
				*dst++ = pixel->g;
				*dst++ = pixel->b;
			}
		}

		dst += (desc->pitch - desc->width) * bytes_per_pixel;
	}

	k_mutex_unlock(&data->fb_lock);

	return 0;
}

static int led_strip_pov_get_capabilities(const struct device *dev,
				 struct display_capabilities *caps)
{
	const struct led_strip_pov_config *config = dev->config;

	memset(caps, 0, sizeof(*caps));

	caps->x_resolution = config->width;
	caps->y_resolution = config->height;
	caps->supported_pixel_formats = PIXEL_FORMAT_ARGB_8888 | PIXEL_FORMAT_RGB_888;
	caps->current_pixel_format = config->pixel_format;

	return 0;
}

static int led_strip_pov_set_pixel_format(const struct device *dev,
				  enum display_pixel_format pixel_format)
{
	const struct led_strip_pov_config *config = dev->config;

	if (pixel_format == config->pixel_format) {
		return 0;
	}

	return -ENOTSUP;
}

static int led_strip_pov_blanking_on(const struct device *dev)
{
	struct led_strip_pov_data *data = dev->data;
	const struct led_strip_pov_config *config = dev->config;

	led_strip_pov_apply_blank(data, config, true);

	return 0;
}

static int led_strip_pov_blanking_off(const struct device *dev)
{
	struct led_strip_pov_data *data = dev->data;
	const struct led_strip_pov_config *config = dev->config;

	led_strip_pov_apply_blank(data, config, false);

	return 0;
}

static uint32_t led_strip_pov_interval_from_rpm(uint32_t rpm, uint16_t width)
{
	uint64_t period_us;

	if (rpm == 0U || width == 0U) {
		return 0U;
	}

	period_us = (uint64_t)USEC_PER_SEC * SEC_PER_MIN;
	period_us /= rpm;
	period_us /= width;

	if (period_us == 0U) {
		period_us = 1U;
	}

	return (uint32_t)period_us;
}

static void led_strip_pov_sensor_thread(void *p1, void *p2, void *p3)
{
	struct led_strip_pov_data *data = p1;
	const struct led_strip_pov_config *config = data->dev->config;
	uint32_t last_rpm = data->active_rpm;
	int64_t last_success = k_uptime_get();
	uint32_t poll_ms = config->sensor_poll_ms ? config->sensor_poll_ms :
		CONFIG_LED_STRIP_POV_SENSOR_POLL_INTERVAL_MS;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		struct sensor_value rpm_val;
		uint32_t next_rpm = last_rpm;
		int ret;

		ret = sensor_sample_fetch(config->tach);
		if (ret == 0) {
			ret = sensor_channel_get(config->tach, SENSOR_CHAN_RPM, &rpm_val);
		}

		if (ret == 0) {
			if (rpm_val.val1 > 0) {
				next_rpm = (uint32_t)rpm_val.val1;
			} else {
				next_rpm = config->default_rpm;
			}
			last_success = k_uptime_get();
		} else if (ret == -EAGAIN || ret == -ETIMEDOUT) {
			int64_t elapsed = k_uptime_get() - last_success;

			if (config->rpm_timeout_ms == 0U || elapsed >= config->rpm_timeout_ms) {
				next_rpm = config->default_rpm;
			}
		} else if (ret < 0) {
			LOG_DBG("tach fetch failed: %d", ret);
		}

		if (next_rpm == 0U) {
			next_rpm = config->default_rpm;
		}

		if (next_rpm != last_rpm) {
			uint32_t interval_us = led_strip_pov_interval_from_rpm(next_rpm,
						 config->width);

			led_strip_pov_update_timer(data, interval_us);
			last_rpm = next_rpm;
			data->active_rpm = next_rpm;
		}

		k_sleep(K_MSEC(MAX(poll_ms, 1U)));
	}
}

static DEVICE_API(display, led_strip_pov_api) = {
	.write = led_strip_pov_write,
	.read = led_strip_pov_read,
	.get_capabilities = led_strip_pov_get_capabilities,
	.set_pixel_format = led_strip_pov_set_pixel_format,
	.blanking_on = led_strip_pov_blanking_on,
	.blanking_off = led_strip_pov_blanking_off,
};

static int led_strip_pov_init(const struct device *dev)
{
	struct led_strip_pov_data *data = dev->data;
	const struct led_strip_pov_config *config = dev->config;
	uint32_t interval_us;

	if (!device_is_ready(config->strip)) {
		LOG_ERR("LED strip %s not ready", config->strip->name);
		return -ENODEV;
	}

	if (!device_is_ready(config->tach)) {
		LOG_ERR("Tachometer %s not ready", config->tach->name);
		return -ENODEV;
	}

	data->dev = dev;
	data->current_column = 0U;
	data->blanked = false;
	data->active_rpm = config->default_rpm;

	k_mutex_init(&data->fb_lock);
	k_timer_init(&data->column_timer, led_strip_pov_timer_handler, NULL);
	k_work_init(&data->column_work, led_strip_pov_column_work);

	memset(data->column_buf, 0, sizeof(struct led_rgb) * config->height);
	(void)led_strip_update_rgb(config->strip, data->column_buf, config->height);

	interval_us = led_strip_pov_interval_from_rpm(config->default_rpm, config->width);
	led_strip_pov_update_timer(data, interval_us);

	k_thread_create(&data->sensor_thread, config->sensor_stack,
		config->sensor_stack_size,
		led_strip_pov_sensor_thread, data, NULL, NULL,
		CONFIG_LED_STRIP_POV_SENSOR_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&data->sensor_thread, "led_strip_pov");

	return 0;
}

#define LED_STRIP_POV_FRAMEBUFFER(inst)                                                             \
	static struct led_rgb led_strip_pov_framebuffer_##inst[                                          \
		DT_INST_PROP(inst, width) * DT_INST_PROP(inst, height)];

#define LED_STRIP_POV_COLUMN_BUF(inst)                                                              \
	static struct led_rgb led_strip_pov_column_buf_##inst[DT_INST_PROP(inst, height)];

#define LED_STRIP_POV_THREAD_STACK(inst)                                                            \
	K_THREAD_STACK_DEFINE(led_strip_pov_stack_##inst, CONFIG_LED_STRIP_POV_SENSOR_THREAD_STACK_SIZE);

#define LED_STRIP_POV_INIT(inst)\
	BUILD_ASSERT(DT_PROP(DT_INST_PHANDLE(inst, led_strip), chain_length) ==\
		DT_INST_PROP(inst, height),\
		"Display height must match LED strip chain length");\
	BUILD_ASSERT((DT_INST_PROP(inst, pixel_format) == PIXEL_FORMAT_RGB_888) ||\
		(DT_INST_PROP(inst, pixel_format) == PIXEL_FORMAT_ARGB_8888),\
		"Unsupported pixel format");\
	LED_STRIP_POV_FRAMEBUFFER(inst);\
	LED_STRIP_POV_COLUMN_BUF(inst);\
	LED_STRIP_POV_THREAD_STACK(inst);\
	static struct led_strip_pov_data led_strip_pov_data_##inst = {\
		.framebuffer = led_strip_pov_framebuffer_##inst,\
		.column_buf = led_strip_pov_column_buf_##inst,\
	};\
	static const struct led_strip_pov_config led_strip_pov_config_##inst = {\
		.strip = DEVICE_DT_GET(DT_INST_PHANDLE(inst, led_strip)),\
		.tach = DEVICE_DT_GET(DT_INST_PHANDLE(inst, tachometer)),\
		.width = DT_INST_PROP(inst, width),\
		.height = DT_INST_PROP(inst, height),\
		.pixel_format = DT_INST_PROP(inst, pixel_format),\
		.default_rpm = DT_INST_PROP_OR(inst, default_rpm,\
			CONFIG_LED_STRIP_POV_DEFAULT_RPM),\
		.rpm_timeout_ms = DT_INST_PROP_OR(inst, rpm_timeout_ms, 0),\
		.sensor_poll_ms = DT_INST_PROP_OR(inst, sensor_poll_ms,\
			CONFIG_LED_STRIP_POV_SENSOR_POLL_INTERVAL_MS),\
		.sensor_stack = led_strip_pov_stack_##inst,\
		.sensor_stack_size = K_THREAD_STACK_SIZEOF(led_strip_pov_stack_##inst),\
	};\
	DEVICE_DT_INST_DEFINE(inst, led_strip_pov_init, NULL, &led_strip_pov_data_##inst,\
		&led_strip_pov_config_##inst, POST_KERNEL,\
		CONFIG_DISPLAY_INIT_PRIORITY, &led_strip_pov_api);
DT_INST_FOREACH_STATUS_OKAY(LED_STRIP_POV_INIT)
