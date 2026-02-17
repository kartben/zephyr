/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>

/*
 * Force a __device_dts_ord_* reference so DT Doctor can diagnose build failures.
 */
static const struct device *const dtdoctor_dev = DEVICE_DT_GET(DT_NODELABEL(dtdoctor_target));

int main(void)
{
	return device_is_ready(dtdoctor_dev) ? 0 : 0;
}
