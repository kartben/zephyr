/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT epson_escpos

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/misc/epson_escpos/epson_escpos.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(epson_escpos, CONFIG_EPSON_ESCPOS_LOG_LEVEL);

/* ESC/POS command bytes */
#define ESCPOS_ESC 0x1B
#define ESCPOS_GS  0x1D
#define ESCPOS_LF  0x0A

/* ESC commands */
#define ESCPOS_CMD_INIT      '@' /* ESC @ - Initialize printer */
#define ESCPOS_CMD_JUSTIFY   'a' /* ESC a n - Set justification */
#define ESCPOS_CMD_BOLD      'E' /* ESC E n - Set bold */
#define ESCPOS_CMD_UNDERLINE '-' /* ESC - n - Set underline */

/* GS commands */
#define ESCPOS_CMD_CUT       'V' /* GS V m - Cut paper */
#define ESCPOS_CMD_TEXT_SIZE '!' /* GS ! n - Set text size */

struct escpos_config {
	const struct device *uart;
};

static int escpos_send_bytes(const struct device *dev, const uint8_t *data, uint16_t len)
{
	const struct escpos_config *config = dev->config;
	const struct device *uart = config->uart;

	for (uint16_t i = 0; i < len; i++) {
		uart_poll_out(uart, data[i]);
	}

	return 0;
}

int escpos_init_printer(const struct device *dev)
{
	uint8_t cmd[] = {ESCPOS_ESC, ESCPOS_CMD_INIT};

	LOG_DBG("Initialize printer");
	return escpos_send_bytes(dev, cmd, sizeof(cmd));
}

int escpos_print(const struct device *dev, const uint8_t *data, uint16_t len)
{
	if (data == NULL || len == 0) {
		return -EINVAL;
	}

	return escpos_send_bytes(dev, data, len);
}

int escpos_println(const struct device *dev, const uint8_t *data, uint16_t len)
{
	int rc;

	if (data == NULL || len == 0) {
		return -EINVAL;
	}

	rc = escpos_send_bytes(dev, data, len);
	if (rc != 0) {
		return rc;
	}

	return escpos_send_bytes(dev, (const uint8_t[]){ESCPOS_LF}, 1);
}

int escpos_feed(const struct device *dev, uint8_t lines)
{
	uint8_t cmd[] = {ESCPOS_ESC, 'd', lines};

	LOG_DBG("Feed %u lines", lines);
	return escpos_send_bytes(dev, cmd, sizeof(cmd));
}

int escpos_cut(const struct device *dev)
{
	/* GS V 0 - Full cut */
	uint8_t cmd[] = {ESCPOS_GS, ESCPOS_CMD_CUT, 0};

	LOG_DBG("Full cut");
	return escpos_send_bytes(dev, cmd, sizeof(cmd));
}

int escpos_cut_partial(const struct device *dev)
{
	/* GS V 1 - Partial cut */
	uint8_t cmd[] = {ESCPOS_GS, ESCPOS_CMD_CUT, 1};

	LOG_DBG("Partial cut");
	return escpos_send_bytes(dev, cmd, sizeof(cmd));
}

int escpos_set_justify(const struct device *dev, enum escpos_justify justify)
{
	uint8_t cmd[3];

	if (justify > ESCPOS_JUSTIFY_RIGHT) {
		return -EINVAL;
	}

	cmd[0] = ESCPOS_ESC;
	cmd[1] = ESCPOS_CMD_JUSTIFY;
	cmd[2] = (uint8_t)justify;

	LOG_DBG("Set justify to %u", justify);
	return escpos_send_bytes(dev, cmd, sizeof(cmd));
}

int escpos_set_bold(const struct device *dev, bool enabled)
{
	uint8_t cmd[] = {ESCPOS_ESC, ESCPOS_CMD_BOLD, enabled ? 1 : 0};

	LOG_DBG("Set bold %s", enabled ? "on" : "off");
	return escpos_send_bytes(dev, cmd, sizeof(cmd));
}

int escpos_set_underline(const struct device *dev, bool enabled)
{
	uint8_t cmd[] = {ESCPOS_ESC, ESCPOS_CMD_UNDERLINE, enabled ? 1 : 0};

	LOG_DBG("Set underline %s", enabled ? "on" : "off");
	return escpos_send_bytes(dev, cmd, sizeof(cmd));
}

int escpos_set_text_size(const struct device *dev, uint8_t width, uint8_t height)
{
	uint8_t size_byte;
	uint8_t cmd[3];

	if (width < 1 || width > 8 || height < 1 || height > 8) {
		return -EINVAL;
	}

	/* GS ! n - n encodes both width and height multipliers */
	/* Bits 0-2: height-1, Bits 4-6: width-1 */
	size_byte = ((width - 1) << 4) | (height - 1);
	cmd[0] = ESCPOS_GS;
	cmd[1] = ESCPOS_CMD_TEXT_SIZE;
	cmd[2] = size_byte;

	LOG_DBG("Set text size width=%u height=%u", width, height);
	return escpos_send_bytes(dev, cmd, sizeof(cmd));
}

int escpos_reset_formatting(const struct device *dev)
{
	int rc;

	rc = escpos_set_bold(dev, false);
	if (rc != 0) {
		return rc;
	}

	rc = escpos_set_underline(dev, false);
	if (rc != 0) {
		return rc;
	}

	rc = escpos_set_text_size(dev, 1, 1);
	if (rc != 0) {
		return rc;
	}

	return escpos_set_justify(dev, ESCPOS_JUSTIFY_LEFT);
}

static int escpos_driver_init(const struct device *dev)
{
	const struct escpos_config *config = dev->config;

	LOG_DBG("Initializing ESC/POS printer driver");

	if (!device_is_ready(config->uart)) {
		LOG_ERR("UART device not ready");
		return -ENODEV;
	}

	return 0;
}

#define ESCPOS_DEVICE(inst)                                                                        \
	static const struct escpos_config escpos_config_##inst = {                                 \
		.uart = DEVICE_DT_GET(DT_INST_BUS(inst)),                                          \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, escpos_driver_init, NULL, NULL, &escpos_config_##inst,         \
			      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_INST_FOREACH_STATUS_OKAY(ESCPOS_DEVICE)
