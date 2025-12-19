/*
 * Copyright (c) 2025 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT epson_escp

#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/misc/escp_printer/escp_printer.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(escp_printer, CONFIG_ESCP_PRINTER_LOG_LEVEL);

/* ESC/P command codes */
#define ESCP_ESC       0x1B /* ESC character */
#define ESCP_LF        0x0A /* Line feed */
#define ESCP_FF        0x0C /* Form feed */
#define ESCP_CR        0x0D /* Carriage return */

struct escp_config {
	const struct device *uart_dev;
};

static int escp_send_bytes(const struct device *dev, const uint8_t *data, uint32_t size)
{
	const struct escp_config *config = dev->config;

	for (uint32_t i = 0; i < size; i++) {
		uart_poll_out(config->uart_dev, data[i]);
	}

	return 0;
}

int escp_printer_init(const struct device *dev)
{
	/* ESC @ - Initialize printer */
	static const uint8_t cmd[] = {ESCP_ESC, '@'};

	LOG_DBG("Initializing printer");
	return escp_send_bytes(dev, cmd, sizeof(cmd));
}

int escp_printer_print(const struct device *dev, const char *data, uint32_t size)
{
	return escp_send_bytes(dev, (const uint8_t *)data, size);
}

int escp_printer_println(const struct device *dev, const char *data, uint32_t size)
{
	int ret;

	ret = escp_printer_print(dev, data, size);
	if (ret < 0) {
		return ret;
	}

	return escp_printer_line_feed(dev);
}

int escp_printer_line_feed(const struct device *dev)
{
	static const uint8_t cmd[] = {ESCP_CR, ESCP_LF};

	return escp_send_bytes(dev, cmd, sizeof(cmd));
}

int escp_printer_form_feed(const struct device *dev)
{
	static const uint8_t cmd[] = {ESCP_FF};

	return escp_send_bytes(dev, cmd, sizeof(cmd));
}

int escp_printer_set_bold(const struct device *dev, bool enable)
{
	/* ESC E - Turn emphasized (bold) mode on/off */
	uint8_t cmd[] = {ESCP_ESC, 'E', enable ? 1 : 0};

	LOG_DBG("Setting bold: %s", enable ? "on" : "off");
	return escp_send_bytes(dev, cmd, sizeof(cmd));
}

int escp_printer_set_italic(const struct device *dev, bool enable)
{
	/* ESC 4 - Turn italic on, ESC 5 - Turn italic off */
	uint8_t cmd[] = {ESCP_ESC, enable ? '4' : '5'};

	LOG_DBG("Setting italic: %s", enable ? "on" : "off");
	return escp_send_bytes(dev, cmd, sizeof(cmd));
}

int escp_printer_set_underline(const struct device *dev, bool enable)
{
	/* ESC - n - Turn underline on/off */
	uint8_t cmd[] = {ESCP_ESC, '-', enable ? 1 : 0};

	LOG_DBG("Setting underline: %s", enable ? "on" : "off");
	return escp_send_bytes(dev, cmd, sizeof(cmd));
}

static int escp_device_init(const struct device *dev)
{
	const struct escp_config *config = dev->config;

	LOG_DBG("Initializing ESC/P printer driver");

	if (!device_is_ready(config->uart_dev)) {
		LOG_ERR("UART device not ready");
		return -ENODEV;
	}

	return 0;
}

#define ESCP_PRINTER_INIT(inst)                                                \
	static const struct escp_config escp_config_##inst = {                 \
		.uart_dev = DEVICE_DT_GET(DT_INST_BUS(inst)),                  \
	};                                                                     \
                                                                               \
	DEVICE_DT_INST_DEFINE(inst, escp_device_init, NULL, NULL,              \
			      &escp_config_##inst, POST_KERNEL,                \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_INST_FOREACH_STATUS_OKAY(ESCP_PRINTER_INIT)
