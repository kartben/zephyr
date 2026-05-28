/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_TESTS_DRIVERS_PINCTRL_COMMON_TEST_PROVIDER_H_
#define ZEPHYR_TESTS_DRIVERS_PINCTRL_COMMON_TEST_PROVIDER_H_

#include <zephyr/device.h>

struct test_pinctrl_provider_data {
	uint32_t call_count;
	uint32_t last_pin;
	uint32_t last_value;
};

const struct test_pinctrl_provider_data *test_pinctrl_provider_data_get(const struct device *dev);
void test_pinctrl_provider_reset(const struct device *dev);

#endif /* ZEPHYR_TESTS_DRIVERS_PINCTRL_COMMON_TEST_PROVIDER_H_ */
