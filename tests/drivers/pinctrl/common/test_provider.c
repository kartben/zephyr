/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT vnd_pinctrl_provider

#include "test_provider.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/pinctrl.h>

static int test_pinctrl_provider_configure_pin(const struct device *dev, uint32_t pin, uint32_t value,
					       uintptr_t reg)
{
	struct test_pinctrl_provider_data *data = dev->data;

	ARG_UNUSED(reg);

	data->call_count++;
	data->last_pin = pin;
	data->last_value = value;

	return 0;
}

const struct test_pinctrl_provider_data *test_pinctrl_provider_data_get(const struct device *dev)
{
	return dev->data;
}

void test_pinctrl_provider_reset(const struct device *dev)
{
	struct test_pinctrl_provider_data *data = dev->data;

	memset(data, 0, sizeof(*data));
}

static const struct pinctrl_driver_api test_pinctrl_provider_api = {
	.configure_pin = test_pinctrl_provider_configure_pin,
};

#define TEST_PINCTRL_PROVIDER_INIT(inst)                                                           \
	static struct test_pinctrl_provider_data test_pinctrl_provider_data_##inst;               \
	DEVICE_DT_INST_DEFINE(inst, NULL, NULL, &test_pinctrl_provider_data_##inst, NULL,         \
			      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                 \
			      &test_pinctrl_provider_api);

DT_INST_FOREACH_STATUS_OKAY(TEST_PINCTRL_PROVIDER_INIT)
