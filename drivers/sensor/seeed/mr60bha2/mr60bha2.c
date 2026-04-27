/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>

#define DT_DRV_COMPAT seeed_mr60bha2

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/mr60bha2.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>

LOG_MODULE_REGISTER(mr60bha2, CONFIG_SENSOR_LOG_LEVEL);

#define MR60BHA2_SOF                     0x01U
#define MR60BHA2_HEADER_SIZE             8U
#define MR60BHA2_DATA_CHECKSUM_SIZE      1U
#define MR60BHA2_MAX_TARGETS             3U
#define MR60BHA2_TARGET_ENTRY_SIZE       16U
#define MR60BHA2_MAX_PAYLOAD_SIZE        (4U + (MR60BHA2_MAX_TARGETS * MR60BHA2_TARGET_ENTRY_SIZE))
#define MR60BHA2_MAX_FRAME_SIZE          (MR60BHA2_HEADER_SIZE + MR60BHA2_MAX_PAYLOAD_SIZE + \
					  MR60BHA2_DATA_CHECKSUM_SIZE)
#define MR60BHA2_TYPE_TARGET_INFO        0x0A04U
#define MR60BHA2_TYPE_BREATH_RATE        0x0A14U
#define MR60BHA2_TYPE_HEART_RATE         0x0A15U
#define MR60BHA2_TYPE_DISTANCE           0x0A16U
#define MR60BHA2_TYPE_HUMAN_DETECTION    0x0F09U
#define MR60BHA2_TYPE_POINT_CLOUD        0x0A08U

enum mr60bha2_rx_state {
	MR60BHA2_RX_WAIT_SOF,
	MR60BHA2_RX_READ_HEADER,
	MR60BHA2_RX_READ_BODY,
};

struct mr60bha2_config {
	const struct device *uart_dev;
	uart_irq_callback_user_data_t cb;
};

struct mr60bha2_data {
	struct k_sem frame_sem;
	struct k_spinlock lock;

	bool presence_valid;
	bool distance_valid;
	bool breath_rate_valid;
	bool heart_rate_valid;
	bool target_count_valid;

	bool presence;
	float distance_m;
	float breath_rate_bpm;
	float heart_rate_bpm;
	uint16_t target_count;

	uint8_t rx_buf[MR60BHA2_MAX_FRAME_SIZE];
	size_t rx_pos;
	size_t frame_len;
	enum mr60bha2_rx_state rx_state;
};

static void mr60bha2_uart_flush(const struct device *uart_dev)
{
	uint8_t c;

	while (uart_fifo_read(uart_dev, &c, 1) > 0) {
	}
}

static void mr60bha2_rx_reset(struct mr60bha2_data *data)
{
	data->rx_pos = 0;
	data->frame_len = 0;
	data->rx_state = MR60BHA2_RX_WAIT_SOF;
}

static uint8_t mr60bha2_checksum(const uint8_t *buf, size_t len)
{
	uint8_t checksum = 0U;

	for (size_t i = 0; i < len; i++) {
		checksum ^= buf[i];
	}

	return ~checksum;
}

static bool mr60bha2_validate_checksum(const uint8_t *buf, size_t len, uint8_t expected)
{
	return mr60bha2_checksum(buf, len) == expected;
}

static float mr60bha2_decode_float_le(const uint8_t *buf)
{
	uint32_t raw = sys_get_le32(buf);
	float value;

	memcpy(&value, &raw, sizeof(value));

	return value;
}

static void mr60bha2_handle_frame(struct mr60bha2_data *data)
{
	const uint8_t *frame = data->rx_buf;
	const uint16_t payload_len = sys_get_be16(&frame[3]);
	const uint16_t frame_type = sys_get_be16(&frame[5]);
	const uint8_t *payload = &frame[MR60BHA2_HEADER_SIZE];
	bool notify = false;
	k_spinlock_key_t key;

	if (!mr60bha2_validate_checksum(frame, MR60BHA2_HEADER_SIZE - 1U, frame[7])) {
		LOG_DBG("Invalid MR60BHA2 header checksum");
		return;
	}

	if (!mr60bha2_validate_checksum(payload, payload_len, frame[data->frame_len - 1U])) {
		LOG_DBG("Invalid MR60BHA2 payload checksum");
		return;
	}

	key = k_spin_lock(&data->lock);

	switch (frame_type) {
	case MR60BHA2_TYPE_HUMAN_DETECTION:
		if (payload_len >= 1U) {
			data->presence = payload[0] != 0U;
			data->presence_valid = true;
			notify = true;
		}
		break;
	case MR60BHA2_TYPE_DISTANCE:
		if (payload_len >= 8U) {
			if (sys_get_le32(payload) != 0U) {
				data->distance_m = mr60bha2_decode_float_le(&payload[4]);
				data->distance_valid = true;
			} else {
				data->distance_m = 0.0f;
				data->distance_valid = false;
			}
			notify = true;
		}
		break;
	case MR60BHA2_TYPE_BREATH_RATE:
		if (payload_len >= 4U) {
			data->breath_rate_bpm = mr60bha2_decode_float_le(payload);
			data->breath_rate_valid = true;
			notify = true;
		}
		break;
	case MR60BHA2_TYPE_HEART_RATE:
		if (payload_len >= 4U) {
			data->heart_rate_bpm = mr60bha2_decode_float_le(payload);
			data->heart_rate_valid = true;
			notify = true;
		}
		break;
	case MR60BHA2_TYPE_TARGET_INFO:
	case MR60BHA2_TYPE_POINT_CLOUD:
		if (payload_len >= 4U) {
			uint32_t targets = sys_get_le32(payload);

			if (targets > MR60BHA2_MAX_TARGETS) {
				LOG_DBG("Clamping reported target count %u to %u", targets,
					MR60BHA2_MAX_TARGETS);
				targets = MR60BHA2_MAX_TARGETS;
			}

			data->target_count = (uint16_t)targets;
			data->target_count_valid = true;
			notify = true;
		}
		break;
	default:
		break;
	}

	k_spin_unlock(&data->lock, key);

	if (notify) {
		k_sem_give(&data->frame_sem);
	}
}

static void mr60bha2_uart_isr(const struct device *uart_dev, void *user_data)
{
	struct mr60bha2_data *data = user_data;
	uint8_t byte;

	if (!uart_irq_update(uart_dev) || !uart_irq_rx_ready(uart_dev)) {
		return;
	}

	while (uart_fifo_read(uart_dev, &byte, 1) == 1) {
		switch (data->rx_state) {
		case MR60BHA2_RX_WAIT_SOF:
			if (byte == MR60BHA2_SOF) {
				data->rx_buf[0] = byte;
				data->rx_pos = 1U;
				data->rx_state = MR60BHA2_RX_READ_HEADER;
			}
			break;
		case MR60BHA2_RX_READ_HEADER:
			data->rx_buf[data->rx_pos++] = byte;
			if (data->rx_pos == MR60BHA2_HEADER_SIZE) {
				uint16_t payload_len = sys_get_be16(&data->rx_buf[3]);

				if (payload_len == 0U || payload_len > MR60BHA2_MAX_PAYLOAD_SIZE) {
					mr60bha2_rx_reset(data);
					if (byte == MR60BHA2_SOF) {
						data->rx_buf[0] = byte;
						data->rx_pos = 1U;
						data->rx_state = MR60BHA2_RX_READ_HEADER;
					}
					break;
				}

				data->frame_len = MR60BHA2_HEADER_SIZE + payload_len +
						MR60BHA2_DATA_CHECKSUM_SIZE;
				data->rx_state = MR60BHA2_RX_READ_BODY;
			}
			break;
		case MR60BHA2_RX_READ_BODY:
			data->rx_buf[data->rx_pos++] = byte;
			if (data->rx_pos == data->frame_len) {
				mr60bha2_handle_frame(data);
				mr60bha2_rx_reset(data);
			}
			break;
		default:
			mr60bha2_rx_reset(data);
			break;
		}
	}
}

static int mr60bha2_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	struct mr60bha2_data *data = dev->data;
	int rc;

	if (chan != SENSOR_CHAN_ALL) {
		return -ENOTSUP;
	}

	rc = k_sem_take(&data->frame_sem, K_MSEC(CONFIG_MR60BHA2_UART_TIMEOUT_MS));
	if (rc != 0) {
		LOG_DBG("Timed out waiting for MR60BHA2 frame");
		return -ETIMEDOUT;
	}

	return 0;
}

static int mr60bha2_channel_get(const struct device *dev, enum sensor_channel chan,
				struct sensor_value *val)
{
	struct mr60bha2_data *data = dev->data;
	k_spinlock_key_t key;
	int rc = 0;

	key = k_spin_lock(&data->lock);

	switch ((uint32_t)chan) {
	case SENSOR_CHAN_PROX:
		if (!data->presence_valid) {
			rc = -ENODATA;
			break;
		}

		val->val1 = data->presence ? 1 : 0;
		val->val2 = 0;
		break;
	case SENSOR_CHAN_DISTANCE:
		if (!data->distance_valid) {
			rc = -ENODATA;
			break;
		}

		rc = sensor_value_from_micro(val, (int64_t)(data->distance_m * 1000000.0f));
		break;
	case SENSOR_CHAN_MR60BHA2_BREATH_RATE:
		if (!data->breath_rate_valid) {
			rc = -ENODATA;
			break;
		}

		rc = sensor_value_from_micro(val, (int64_t)(data->breath_rate_bpm * 1000000.0f));
		break;
	case SENSOR_CHAN_MR60BHA2_HEART_RATE:
		if (!data->heart_rate_valid) {
			rc = -ENODATA;
			break;
		}

		rc = sensor_value_from_micro(val, (int64_t)(data->heart_rate_bpm * 1000000.0f));
		break;
	case SENSOR_CHAN_MR60BHA2_TARGET_COUNT:
		if (!data->target_count_valid) {
			rc = -ENODATA;
			break;
		}

		val->val1 = data->target_count;
		val->val2 = 0;
		break;
	default:
		rc = -ENOTSUP;
		break;
	}

	k_spin_unlock(&data->lock, key);

	return rc;
}

static DEVICE_API(sensor, mr60bha2_driver_api) = {
	.sample_fetch = mr60bha2_sample_fetch,
	.channel_get = mr60bha2_channel_get,
};

static int mr60bha2_init(const struct device *dev)
{
	const struct mr60bha2_config *config = dev->config;
	struct mr60bha2_data *data = dev->data;
	int rc;

	if (!device_is_ready(config->uart_dev)) {
		LOG_ERR("UART device %s is not ready", config->uart_dev->name);
		return -ENODEV;
	}

	k_sem_init(&data->frame_sem, 0, K_SEM_MAX_LIMIT);
	mr60bha2_rx_reset(data);

	uart_irq_rx_disable(config->uart_dev);
	uart_irq_tx_disable(config->uart_dev);
	mr60bha2_uart_flush(config->uart_dev);

	rc = uart_irq_callback_user_data_set(config->uart_dev, config->cb, data);
	if (rc != 0) {
		LOG_ERR("UART IRQ setup failed: %d", rc);
		return rc;
	}

	uart_irq_rx_enable(config->uart_dev);

	return 0;
}

#define MR60BHA2_DEFINE(inst)                                                                      \
	static struct mr60bha2_data mr60bha2_data_##inst;                                         \
                                                                                                   \
	static const struct mr60bha2_config mr60bha2_config_##inst = {                            \
		.uart_dev = DEVICE_DT_GET(DT_INST_BUS(inst)),                                     \
		.cb = mr60bha2_uart_isr,                                                          \
	};                                                                                        \
                                                                                                   \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, mr60bha2_init, NULL, &mr60bha2_data_##inst,           \
				     &mr60bha2_config_##inst, POST_KERNEL,                        \
				     CONFIG_SENSOR_INIT_PRIORITY, &mr60bha2_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MR60BHA2_DEFINE)
