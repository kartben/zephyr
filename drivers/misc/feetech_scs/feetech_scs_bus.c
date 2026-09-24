/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT feetech_scs_bus

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "feetech_scs_bus.h"

LOG_MODULE_REGISTER(feetech_scs_bus, CONFIG_FEETECH_SCS_LOG_LEVEL);

#define SCS_HEADER 0xffU

/* Header (2), ID, length, instruction or status, checksum */
#define SCS_PACKET_OVERHEAD 6U

/* Room for a full answer plus stray bytes received before it */
#define SCS_RX_BUF_SIZE 32U

BUILD_ASSERT(SCS_PACKET_OVERHEAD + FEETECH_SCS_MAX_PARAMS <= SCS_RX_BUF_SIZE);

struct feetech_scs_bus_config {
	const struct device *uart;
};

struct feetech_scs_bus_data {
	struct k_mutex lock;
	struct k_sem rx_sem;
	atomic_t rx_len;
	uint8_t rx_buf[SCS_RX_BUF_SIZE];
};

static void feetech_scs_bus_isr(const struct device *uart, void *user_data)
{
	struct feetech_scs_bus_data *data = user_data;
	uint8_t discard;
	size_t len;
	int n;

	uart_irq_update(uart);

	while (uart_irq_rx_ready(uart) > 0) {
		len = (size_t)atomic_get(&data->rx_len);
		if (len >= sizeof(data->rx_buf)) {
			/* Nothing is waiting for these bytes, drop them */
			if (uart_fifo_read(uart, &discard, sizeof(discard)) <= 0) {
				break;
			}
			continue;
		}

		n = uart_fifo_read(uart, &data->rx_buf[len], sizeof(data->rx_buf) - len);
		if (n <= 0) {
			break;
		}

		atomic_add(&data->rx_len, n);
		k_sem_give(&data->rx_sem);
	}
}

static int feetech_scs_bus_wait_rx(struct feetech_scs_bus_data *data, size_t count,
				   k_timepoint_t end)
{
	if (count > sizeof(data->rx_buf)) {
		return -EIO;
	}

	while ((size_t)atomic_get(&data->rx_len) < count) {
		if (k_sem_take(&data->rx_sem, sys_timepoint_timeout(end)) != 0) {
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static uint8_t feetech_scs_checksum(const uint8_t *buf, size_t len)
{
	uint8_t sum = 0U;

	for (size_t i = 0; i < len; i++) {
		sum += buf[i];
	}

	return (uint8_t)~sum;
}

static int feetech_scs_bus_receive(const struct device *bus, uint8_t id, uint8_t *answer,
				   size_t answer_len)
{
	struct feetech_scs_bus_data *data = bus->data;
	k_timepoint_t end = sys_timepoint_calc(K_MSEC(CONFIG_FEETECH_SCS_RESPONSE_TIMEOUT_MS));
	const uint8_t *pkt;
	size_t pos;
	int ret;

	/* Skip anything preceding the header; an ID is never 0xff */
	for (pos = 0U;; pos++) {
		ret = feetech_scs_bus_wait_rx(data, pos + 4U, end);
		if (ret < 0) {
			return ret;
		}

		pkt = &data->rx_buf[pos];
		if (pkt[0] == SCS_HEADER && pkt[1] == SCS_HEADER && pkt[2] != SCS_HEADER) {
			break;
		}
	}

	/* The length field counts the status byte, the payload and the checksum */
	if (pkt[2] != id || pkt[3] != answer_len + 2U) {
		LOG_DBG("Unexpected answer header: ID %u, length %u", pkt[2], pkt[3]);
		return -EIO;
	}

	ret = feetech_scs_bus_wait_rx(data, pos + answer_len + SCS_PACKET_OVERHEAD, end);
	if (ret < 0) {
		return ret;
	}

	if (feetech_scs_checksum(&pkt[2], answer_len + 3U) != pkt[answer_len + 5U]) {
		LOG_DBG("Checksum error in answer from servo %u", id);
		return -EIO;
	}

	if (pkt[4] != 0U) {
		LOG_DBG("Servo %u status 0x%02x", id, pkt[4]);
	}

	if (answer_len > 0U) {
		memcpy(answer, &pkt[5], answer_len);
	}

	return 0;
}

int feetech_scs_bus_transceive(const struct device *bus, uint8_t id, uint8_t instruction,
			       const uint8_t *params, size_t params_len, uint8_t *answer,
			       size_t answer_len)
{
	const struct feetech_scs_bus_config *config = bus->config;
	struct feetech_scs_bus_data *data = bus->data;
	uint8_t tx[SCS_PACKET_OVERHEAD + FEETECH_SCS_MAX_PARAMS];
	size_t tx_len = params_len + SCS_PACKET_OVERHEAD;
	uint8_t c;
	int ret;

	if (params_len > FEETECH_SCS_MAX_PARAMS || answer_len > FEETECH_SCS_MAX_PARAMS) {
		return -EINVAL;
	}

	tx[0] = SCS_HEADER;
	tx[1] = SCS_HEADER;
	tx[2] = id;
	tx[3] = (uint8_t)(params_len + 2U);
	tx[4] = instruction;
	if (params_len > 0U) {
		memcpy(&tx[5], params, params_len);
	}
	tx[tx_len - 1U] = feetech_scs_checksum(&tx[2], tx_len - 3U);

	k_mutex_lock(&data->lock, K_FOREVER);

	/* Flush bytes left over from an earlier, timed out, transaction */
	while (uart_poll_in(config->uart, &c) == 0) {
	}
	atomic_set(&data->rx_len, 0);
	k_sem_reset(&data->rx_sem);
	uart_irq_rx_enable(config->uart);

	for (size_t i = 0; i < tx_len; i++) {
		uart_poll_out(config->uart, tx[i]);
	}

	ret = feetech_scs_bus_receive(bus, id, answer, answer_len);

	uart_irq_rx_disable(config->uart);

	k_mutex_unlock(&data->lock);

	return ret;
}

static int feetech_scs_bus_init(const struct device *dev)
{
	const struct feetech_scs_bus_config *config = dev->config;
	struct feetech_scs_bus_data *data = dev->data;
	int ret;

	if (!device_is_ready(config->uart)) {
		LOG_ERR_DEVICE_NOT_READY(config->uart);
		return -ENODEV;
	}

	k_mutex_init(&data->lock);
	k_sem_init(&data->rx_sem, 0, K_SEM_MAX_LIMIT);

	uart_irq_rx_disable(config->uart);
	uart_irq_tx_disable(config->uart);

	ret = uart_irq_callback_user_data_set(config->uart, feetech_scs_bus_isr, data);
	if (ret < 0) {
		LOG_ERR("Failed to set UART callback: %d", ret);
		return ret;
	}

	return 0;
}

#define FEETECH_SCS_BUS_DEFINE(inst)                                                               \
	static const struct feetech_scs_bus_config feetech_scs_bus_config_##inst = {               \
		.uart = DEVICE_DT_GET(DT_INST_BUS(inst)),                                          \
	};                                                                                         \
	static struct feetech_scs_bus_data feetech_scs_bus_data_##inst;                            \
	DEVICE_DT_INST_DEFINE(inst, feetech_scs_bus_init, NULL, &feetech_scs_bus_data_##inst,      \
			      &feetech_scs_bus_config_##inst, POST_KERNEL,                         \
			      CONFIG_FEETECH_SCS_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(FEETECH_SCS_BUS_DEFINE)
