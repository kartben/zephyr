/*
 * Copyright (c) 2024 Ambiq Micro Inc. <www.ambiq.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/mspi.h>
#include <zephyr/drivers/mspi_emul.h>
#define DT_DRV_COMPAT zephyr_emul_device_mspi

/* Stub out a mspi device struct to use mspi_device emulator. */
static int emul_mspi_device_init_stub(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

static int emul_mspi_init_stub(const struct emul *stub_emul, const struct device *bus)
{
	ARG_UNUSED(stub_emul);
	ARG_UNUSED(bus);

	return 0;
}

#define EMUL_MSPI_DEVICE_DEVICE_STUB(n)                                                           \
	DEVICE_DT_INST_DEFINE(n,                                                                  \
			      emul_mspi_device_init_stub,                                         \
			      NULL,                                                               \
			      NULL,                                                               \
			      NULL,                                                               \
			      POST_KERNEL,                                                        \
			      CONFIG_MSPI_INIT_PRIORITY,                                          \
			      NULL);

#define EMUL_TEST(n)                                                                              \
	EMUL_DT_INST_DEFINE(n,                                                                    \
			    emul_mspi_init_stub,                                                  \
			    NULL,                                                                 \
			    NULL,                                                                 \
			    NULL,                                                                 \
			    NULL);

DT_INST_FOREACH_STATUS_OKAY(EMUL_TEST)

DT_INST_FOREACH_STATUS_OKAY(EMUL_MSPI_DEVICE_DEVICE_STUB)
