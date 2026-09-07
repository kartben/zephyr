/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/device.h>
#include <zephyr/ztest.h>

#define CHECK_READY(node) zassert_true(device_is_ready(DEVICE_DT_GET(node)));

ZTEST(sensor_baseline, test_ready)
{
	DT_FOREACH_CHILD_STATUS_OKAY(DT_NODELABEL(test_i2c), CHECK_READY)
}

ZTEST_SUITE(sensor_baseline, NULL, NULL, NULL, NULL, NULL);
