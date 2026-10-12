/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>

#include "spa06.h"

LOG_MODULE_DECLARE(SPA06, CONFIG_SENSOR_LOG_LEVEL);

static void spa06_handle_interrupt(const struct device *dev)
{
	struct spa06_data *data = dev->data;
	uint8_t int_sts;
	int ret;

#ifdef CONFIG_SPA06_STREAM
	if (spa06_stream_handle_interrupt(dev)) {
		return;
	}
#endif

	/* Reading INT_STS clears it and releases the interrupt line */
	ret = spa06_reg_read(dev, SPA06_REG_INT_STS, &int_sts, 1);
	if (ret < 0) {
		LOG_ERR("Failed to read the interrupt status: %d", ret);
		return;
	}

	if (((int_sts & (SPA06_INT_STS_PRS | SPA06_INT_STS_TMP)) != 0U) &&
	    (data->drdy_handler != NULL)) {
		data->drdy_handler(dev, data->drdy_trigger);
	}
}

#if defined(CONFIG_SPA06_TRIGGER_OWN_THREAD)
static void spa06_thread_main(void *p1, void *p2, void *p3)
{
	const struct device *dev = p1;
	struct spa06_data *data = dev->data;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		k_sem_take(&data->gpio_sem, K_FOREVER);
		spa06_handle_interrupt(dev);
	}
}
#elif defined(CONFIG_SPA06_TRIGGER_GLOBAL_THREAD)
static void spa06_work_handler(struct k_work *work)
{
	struct spa06_data *data = CONTAINER_OF(work, struct spa06_data, work);

	spa06_handle_interrupt(data->dev);
}
#endif

static void spa06_gpio_callback(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	struct spa06_data *data = CONTAINER_OF(cb, struct spa06_data, gpio_cb);

	ARG_UNUSED(port);
	ARG_UNUSED(pins);

#if defined(CONFIG_SPA06_TRIGGER_OWN_THREAD)
	k_sem_give(&data->gpio_sem);
#elif defined(CONFIG_SPA06_TRIGGER_GLOBAL_THREAD)
	k_work_submit(&data->work);
#endif
}

int spa06_trigger_set(const struct device *dev, const struct sensor_trigger *trig,
		      sensor_trigger_handler_t handler)
{
	const struct spa06_config *cfg = dev->config;
	struct spa06_data *data = dev->data;
	uint8_t int_cfg;
	uint8_t int_sts;
	int ret;

	if (trig->type != SENSOR_TRIG_DATA_READY) {
		return -ENOTSUP;
	}

	switch (trig->chan) {
	case SENSOR_CHAN_ALL:
	case SENSOR_CHAN_PRESS:
		int_cfg = SPA06_CFG_REG_INT_PRS;
		break;
	case SENSOR_CHAN_AMBIENT_TEMP:
		int_cfg = SPA06_CFG_REG_INT_TMP;
		break;
	default:
		return -ENOTSUP;
	}

	/* On 4-wire SPI, the interrupt pin is the data output */
	if (cfg->int_gpio.port == NULL) {
		return -ENOTSUP;
	}

#ifdef CONFIG_SPA06_STREAM
	if (data->stream_events != 0U) {
		return -EBUSY;
	}
#endif

	if ((handler != NULL) && (data->prs_odr == SPA06_ODR_NONE)) {
		LOG_ERR("Data-ready trigger needs continuous measurements");
		return -EINVAL;
	}

	data->drdy_handler = handler;
	data->drdy_trigger = trig;
	data->int_cfg = (handler != NULL) ? int_cfg : 0U;

	/* While suspended, the interrupt configuration is applied on resume */
	if (!spa06_is_active(dev)) {
		return 0;
	}

	ret = spa06_write_cfg_reg(dev);
	if ((ret < 0) || (handler == NULL)) {
		return ret;
	}

	/* Clear a pending status, so that the next result asserts the line */
	return spa06_reg_read(dev, SPA06_REG_INT_STS, &int_sts, 1);
}

int spa06_trigger_init(const struct device *dev)
{
	const struct spa06_config *cfg = dev->config;
	struct spa06_data *data = dev->data;
	int ret;

	if (cfg->int_gpio.port == NULL) {
		return 0;
	}

	if (!gpio_is_ready_dt(&cfg->int_gpio)) {
		LOG_ERR_DEVICE_NOT_READY(cfg->int_gpio.port);
		return -ENODEV;
	}

	data->dev = dev;

#if defined(CONFIG_SPA06_TRIGGER_OWN_THREAD)
	k_sem_init(&data->gpio_sem, 0, 1);
	k_thread_create(&data->thread, data->thread_stack,
			K_KERNEL_STACK_SIZEOF(data->thread_stack), spa06_thread_main, (void *)dev,
			NULL, NULL, K_PRIO_COOP(CONFIG_SPA06_THREAD_PRIORITY), 0, K_NO_WAIT);
	k_thread_name_set(&data->thread, dev->name);
#elif defined(CONFIG_SPA06_TRIGGER_GLOBAL_THREAD)
	k_work_init(&data->work, spa06_work_handler);
#endif

	ret = gpio_pin_configure_dt(&cfg->int_gpio, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	gpio_init_callback(&data->gpio_cb, spa06_gpio_callback, BIT(cfg->int_gpio.pin));

	ret = gpio_add_callback_dt(&cfg->int_gpio, &data->gpio_cb);
	if (ret < 0) {
		return ret;
	}

	/* Enabled on resume */
	return spa06_trigger_enable(dev, false);
}

int spa06_trigger_enable(const struct device *dev, bool enable)
{
	const struct spa06_config *cfg = dev->config;

	if (cfg->int_gpio.port == NULL) {
		return 0;
	}

	return gpio_pin_interrupt_configure_dt(&cfg->int_gpio,
					       enable ? GPIO_INT_EDGE_TO_ACTIVE : GPIO_INT_DISABLE);
}
