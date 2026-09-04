/*
 * Copyright 2023 Google LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_EMUL_STUB_DEVICE_H_
#define ZEPHYR_INCLUDE_EMUL_STUB_DEVICE_H_

#include <zephyr/device.h>
#include <zephyr/devicetree.h>

/*
 * Needed for emulators without corresponding DEVICE_DT_DEFINE drivers
 */

/* For every instance of a @c DT_DRV_COMPAT stub out a device for that instance */
#define EMUL_STUB_DEVICE(n)                                                                        \
	__maybe_unused static int emul_init_stub_##n(const struct device *dev)                     \
	{                                                                                          \
		ARG_UNUSED(dev);                                                                   \
		return 0;                                                                          \
	}                                                                                          \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, &emul_init_stub_##n, NULL, NULL, NULL,                            \
			      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

#endif /* ZEPHYR_INCLUDE_EMUL_STUB_DEVICE_H_ */
