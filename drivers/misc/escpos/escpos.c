/*
 * Copyright (c) 2025 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT epson_escpos

#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/misc/escpos/escpos.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(escpos, CONFIG_ESCPOS_LOG_LEVEL);

/* ESC/POS command codes */
#define ESCPOS_ESC       0x1B /* ESC character */
#define ESCPOS_GS        0x1D /* GS character */
#define ESCPOS_LF        0x0A /* Line feed */

struct escpos_config {
	const struct device *uart_dev;
};

static int escpos_send_bytes(const struct device *dev, const uint8_t *data, uint32_t size)
{
	const struct escpos_config *config = dev->config;

	for (uint32_t i = 0; i < size; i++) {
		uart_poll_out(config->uart_dev, data[i]);
	}

	return 0;
}

int escpos_init(const struct device *dev)
{
	/* ESC @ - Initialize printer */
	static const uint8_t cmd[] = {ESCPOS_ESC, '@'};

	LOG_DBG("Initializing printer");
	return escpos_send_bytes(dev, cmd, sizeof(cmd));
}

int escpos_print(const struct device *dev, const char *data, uint32_t size)
{
	return escpos_send_bytes(dev, (const uint8_t *)data, size);
}

int escpos_println(const struct device *dev, const char *data, uint32_t size)
{
	int ret;

	ret = escpos_print(dev, data, size);
	if (ret < 0) {
		return ret;
	}

	return escpos_line_feed(dev);
}

int escpos_line_feed(const struct device *dev)
{
	static const uint8_t cmd[] = {ESCPOS_LF};

	return escpos_send_bytes(dev, cmd, sizeof(cmd));
}

int escpos_cut_paper(const struct device *dev)
{
	/* GS V 0 - Full paper cut */
	static const uint8_t cmd[] = {ESCPOS_GS, 'V', 0};

	LOG_DBG("Cutting paper");
	return escpos_send_bytes(dev, cmd, sizeof(cmd));
}

int escpos_set_bold(const struct device *dev, bool enable)
{
	/* ESC E n - Turn emphasized (bold) mode on/off */
	uint8_t cmd[] = {ESCPOS_ESC, 'E', enable ? 1 : 0};

	LOG_DBG("Setting bold: %s", enable ? "on" : "off");
	return escpos_send_bytes(dev, cmd, sizeof(cmd));
}

int escpos_set_underline(const struct device *dev, bool enable)
{
	/* ESC - n - Turn underline on/off */
	uint8_t cmd[] = {ESCPOS_ESC, '-', enable ? 1 : 0};

	LOG_DBG("Setting underline: %s", enable ? "on" : "off");
	return escpos_send_bytes(dev, cmd, sizeof(cmd));
}

static int escpos_device_init(const struct device *dev)
{
	const struct escpos_config *config = dev->config;

	LOG_DBG("Initializing ESC/POS printer driver");

	if (!device_is_ready(config->uart_dev)) {
		LOG_ERR("UART device not ready");
		return -ENODEV;
	}

	return 0;
}

#define ESCPOS_INIT(inst)                                                      \
	static const struct escpos_config escpos_config_##inst = {             \
		.uart_dev = DEVICE_DT_GET(DT_INST_BUS(inst)),                  \
	};                                                                     \
                                                                               \
	DEVICE_DT_INST_DEFINE(inst, escpos_device_init, NULL, NULL,            \
			      &escpos_config_##inst, POST_KERNEL,              \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_INST_FOREACH_STATUS_OKAY(ESCPOS_INIT)
