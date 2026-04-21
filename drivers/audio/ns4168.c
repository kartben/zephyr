/*
 * Copyright (c) 2024 Arduino SA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT chipsourcetek_ns4168

#include <zephyr/audio/codec.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define LOG_LEVEL CONFIG_AUDIO_CODEC_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ns4168);

struct ns4168_config {
	struct gpio_dt_spec enable_gpio;
};

static int ns4168_configure(const struct device *dev, struct audio_codec_cfg *cfg)
{
	if (cfg->dai_route != AUDIO_ROUTE_PLAYBACK) {
		LOG_ERR("unsupported route %d", cfg->dai_route);
		return -ENOTSUP;
	}

	if (cfg->dai_type != AUDIO_DAI_TYPE_I2S) {
		LOG_ERR("unsupported DAI type %d", cfg->dai_type);
		return -ENOTSUP;
	}

	return 0;
}

static void ns4168_start_output(const struct device *dev)
{
	const struct ns4168_config *cfg = dev->config;
	int ret;

	if (cfg->enable_gpio.port != NULL) {
		ret = gpio_pin_set_dt(&cfg->enable_gpio, 1);
		if (ret < 0) {
			LOG_ERR("Failed to enable amplifier (%d)", ret);
		}
	}
}

static void ns4168_stop_output(const struct device *dev)
{
	const struct ns4168_config *cfg = dev->config;
	int ret;

	if (cfg->enable_gpio.port != NULL) {
		ret = gpio_pin_set_dt(&cfg->enable_gpio, 0);
		if (ret < 0) {
			LOG_ERR("Failed to disable amplifier (%d)", ret);
		}
	}
}

static int ns4168_set_property(const struct device *dev, audio_property_t property,
			       audio_channel_t channel, audio_property_value_t val)
{
	return -ENOTSUP;
}

static int ns4168_apply_properties(const struct device *dev)
{
	return 0;
}

static const struct audio_codec_api ns4168_driver_api = {
	.configure = ns4168_configure,
	.start_output = ns4168_start_output,
	.stop_output = ns4168_stop_output,
	.set_property = ns4168_set_property,
	.apply_properties = ns4168_apply_properties,
};

static int ns4168_init(const struct device *dev)
{
	const struct ns4168_config *cfg = dev->config;
	int ret;

	if (cfg->enable_gpio.port != NULL) {
		if (!device_is_ready(cfg->enable_gpio.port)) {
			LOG_ERR("GPIO device not ready");
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&cfg->enable_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to configure enable GPIO (%d)", ret);
			return ret;
		}
	}

	return 0;
}

#define NS4168_INST(idx)                                                                           \
	static const struct ns4168_config ns4168_config_##idx = {                                  \
		.enable_gpio = GPIO_DT_SPEC_INST_GET_OR(idx, enable_gpios, {0}),                   \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(idx, ns4168_init, NULL, NULL, &ns4168_config_##idx, POST_KERNEL,     \
			      CONFIG_AUDIO_CODEC_INIT_PRIORITY, &ns4168_driver_api)

DT_INST_FOREACH_STATUS_OKAY(NS4168_INST)
