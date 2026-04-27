/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>

#define DT_DRV_COMPAT seeed_mr60bha2

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/mr60bha2.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(mr60bha2, CONFIG_SENSOR_LOG_LEVEL);

/* Frame delimiters */
#define MR60BHA2_FRAME_HEADER_0 0x53
#define MR60BHA2_FRAME_HEADER_1 0x59
#define MR60BHA2_FRAME_TAIL_0   0x54
#define MR60BHA2_FRAME_TAIL_1   0x43

/* Maximum payload length across all known frame types (breathing rate = 2 bytes) */
#define MR60BHA2_MAX_PAYLOAD_LEN 4U

/* Word/command pairs for output frames (sensor → host) */
#define MR60BHA2_WORD_SYS_HEARTBEAT    0x80
#define MR60BHA2_CMD_SYS_HEARTBEAT     0x01
#define MR60BHA2_WORD_PRESENCE         0x01
#define MR60BHA2_CMD_PRESENCE          0x01
#define MR60BHA2_WORD_BREATHING        0x02
#define MR60BHA2_CMD_BREATHING_PHASE   0x01
#define MR60BHA2_CMD_BREATHING_RATE    0x05
#define MR60BHA2_WORD_HEARTBEAT        0x03
#define MR60BHA2_CMD_HEARTBEAT_PHASE   0x01
#define MR60BHA2_CMD_HEARTBEAT_RATE    0x05

/* Bitmask tracking which frame types have been received in a fetch window */
#define MR60BHA2_MASK_PRESENCE       BIT(0)
#define MR60BHA2_MASK_BREATHING_RATE BIT(1)
#define MR60BHA2_MASK_HEARTBEAT_RATE BIT(2)
#define MR60BHA2_MASK_ALL \
	(MR60BHA2_MASK_PRESENCE | MR60BHA2_MASK_BREATHING_RATE | MR60BHA2_MASK_HEARTBEAT_RATE)

enum mr60bha2_rx_state {
	MR60BHA2_RX_WAIT_HEADER,
	MR60BHA2_RX_READ_WORD,
	MR60BHA2_RX_READ_CMD,
	MR60BHA2_RX_READ_LEN_L,
	MR60BHA2_RX_READ_LEN_H,
	MR60BHA2_RX_READ_DATA,
	MR60BHA2_RX_READ_LCC,
	MR60BHA2_RX_READ_TAIL,
};

struct mr60bha2_config {
	const struct device *uart_dev;
	uart_irq_callback_user_data_t cb;
};

struct mr60bha2_data {
	/* Latest decoded sensor values */
	uint8_t presence_status;
	uint8_t breathing_phase;
	uint16_t breathing_rate_tenths; /* raw value; divide by 10 for breaths/min */
	uint8_t heartbeat_phase;
	uint8_t heartbeat_rate_bpm;

	/* Tracks which frame types arrived in the current fetch window */
	uint8_t received_mask;

	/* UART RX state machine */
	struct k_sem lock;
	struct k_sem rx_sem;
	uint8_t rx_buf[MR60BHA2_MAX_PAYLOAD_LEN];
	uint16_t rx_pos;
	uint16_t frame_len;
	uint8_t frame_word;
	uint8_t frame_cmd;
	uint8_t computed_lcc;
	uint8_t header_matched;
	uint8_t trailer_matched;
	enum mr60bha2_rx_state rx_state;
};

static void mr60bha2_uart_flush(const struct device *uart_dev)
{
	uint8_t c;

	while (uart_fifo_read(uart_dev, &c, 1) > 0) {
		continue;
	}
}

static void mr60bha2_rx_reset(struct mr60bha2_data *data)
{
	data->rx_state = MR60BHA2_RX_WAIT_HEADER;
	data->rx_pos = 0;
	data->frame_len = 0;
	data->frame_word = 0;
	data->frame_cmd = 0;
	data->computed_lcc = 0;
	data->header_matched = 0;
	data->trailer_matched = 0;
}

static void mr60bha2_dispatch_frame(struct mr60bha2_data *data)
{
	uint8_t word = data->frame_word;
	uint8_t cmd = data->frame_cmd;

	if (word == MR60BHA2_WORD_SYS_HEARTBEAT && cmd == MR60BHA2_CMD_SYS_HEARTBEAT) {
		/* Keep-alive ping, no data payload */
		return;
	}

	if (word == MR60BHA2_WORD_PRESENCE && cmd == MR60BHA2_CMD_PRESENCE &&
	    data->frame_len >= 1) {
		data->presence_status = data->rx_buf[0];
		data->received_mask |= MR60BHA2_MASK_PRESENCE;
	} else if (word == MR60BHA2_WORD_BREATHING && cmd == MR60BHA2_CMD_BREATHING_PHASE &&
		   data->frame_len >= 1) {
		data->breathing_phase = data->rx_buf[0];
	} else if (word == MR60BHA2_WORD_BREATHING && cmd == MR60BHA2_CMD_BREATHING_RATE &&
		   data->frame_len >= 2) {
		data->breathing_rate_tenths = sys_get_le16(data->rx_buf);
		data->received_mask |= MR60BHA2_MASK_BREATHING_RATE;
	} else if (word == MR60BHA2_WORD_HEARTBEAT && cmd == MR60BHA2_CMD_HEARTBEAT_PHASE &&
		   data->frame_len >= 1) {
		data->heartbeat_phase = data->rx_buf[0];
	} else if (word == MR60BHA2_WORD_HEARTBEAT && cmd == MR60BHA2_CMD_HEARTBEAT_RATE &&
		   data->frame_len >= 1) {
		data->heartbeat_rate_bpm = data->rx_buf[0];
		data->received_mask |= MR60BHA2_MASK_HEARTBEAT_RATE;
	}
}

static void mr60bha2_uart_isr(const struct device *uart_dev, void *user_data)
{
	struct mr60bha2_data *data = user_data;
	uint8_t byte;

	if (!uart_irq_update(uart_dev)) {
		return;
	}

	if (!uart_irq_rx_ready(uart_dev)) {
		return;
	}

	while (uart_fifo_read(uart_dev, &byte, 1) == 1) {
		switch (data->rx_state) {
		case MR60BHA2_RX_WAIT_HEADER:
			if (data->header_matched == 0) {
				if (byte == MR60BHA2_FRAME_HEADER_0) {
					data->header_matched = 1;
				}
			} else {
				if (byte == MR60BHA2_FRAME_HEADER_1) {
					data->header_matched = 0;
					data->computed_lcc = 0;
					data->rx_state = MR60BHA2_RX_READ_WORD;
				} else {
					data->header_matched =
						(byte == MR60BHA2_FRAME_HEADER_0) ? 1 : 0;
				}
			}
			break;

		case MR60BHA2_RX_READ_WORD:
			data->frame_word = byte;
			data->computed_lcc ^= byte;
			data->rx_state = MR60BHA2_RX_READ_CMD;
			break;

		case MR60BHA2_RX_READ_CMD:
			data->frame_cmd = byte;
			data->computed_lcc ^= byte;
			data->rx_state = MR60BHA2_RX_READ_LEN_L;
			break;

		case MR60BHA2_RX_READ_LEN_L:
			data->frame_len = byte;
			data->computed_lcc ^= byte;
			data->rx_state = MR60BHA2_RX_READ_LEN_H;
			break;

		case MR60BHA2_RX_READ_LEN_H:
			data->frame_len |= (uint16_t)byte << 8;
			data->computed_lcc ^= byte;
			data->rx_pos = 0;
			if (data->frame_len > MR60BHA2_MAX_PAYLOAD_LEN) {
				mr60bha2_rx_reset(data);
				break;
			}
			data->rx_state = (data->frame_len == 0) ? MR60BHA2_RX_READ_LCC
								: MR60BHA2_RX_READ_DATA;
			break;

		case MR60BHA2_RX_READ_DATA:
			data->rx_buf[data->rx_pos++] = byte;
			data->computed_lcc ^= byte;
			if (data->rx_pos == data->frame_len) {
				data->rx_state = MR60BHA2_RX_READ_LCC;
			}
			break;

		case MR60BHA2_RX_READ_LCC:
			if (byte != data->computed_lcc) {
				LOG_DBG("LCC mismatch: expected 0x%02x, got 0x%02x",
					data->computed_lcc, byte);
				mr60bha2_rx_reset(data);
				break;
			}
			data->trailer_matched = 0;
			data->rx_state = MR60BHA2_RX_READ_TAIL;
			break;

		case MR60BHA2_RX_READ_TAIL:
			if (data->trailer_matched == 0) {
				if (byte != MR60BHA2_FRAME_TAIL_0) {
					mr60bha2_rx_reset(data);
					break;
				}
				data->trailer_matched = 1;
			} else {
				if (byte != MR60BHA2_FRAME_TAIL_1) {
					mr60bha2_rx_reset(data);
					break;
				}
				mr60bha2_dispatch_frame(data);
				mr60bha2_rx_reset(data);
				if ((data->received_mask & MR60BHA2_MASK_ALL) ==
				    MR60BHA2_MASK_ALL) {
					uart_irq_rx_disable(uart_dev);
					k_sem_give(&data->rx_sem);
					return;
				}
			}
			break;

		default:
			break;
		}
	}
}

static int mr60bha2_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	const struct mr60bha2_config *config = dev->config;
	struct mr60bha2_data *data = dev->data;
	int rc;

	if (chan != SENSOR_CHAN_ALL) {
		LOG_ERR("Unsupported sensor channel");
		return -ENOTSUP;
	}

	k_sem_take(&data->lock, K_FOREVER);

	data->received_mask = 0;
	mr60bha2_rx_reset(data);
	k_sem_reset(&data->rx_sem);
	uart_irq_rx_enable(config->uart_dev);

	rc = k_sem_take(&data->rx_sem, K_MSEC(CONFIG_MR60BHA2_UART_TIMEOUT_MS));

	if (rc != 0) {
		uart_irq_rx_disable(config->uart_dev);

		if (rc == -EAGAIN) {
			LOG_DBG("Did not receive all frame types after %d ms",
				CONFIG_MR60BHA2_UART_TIMEOUT_MS);
			if (data->received_mask == 0) {
				k_sem_give(&data->lock);
				return -ETIMEDOUT;
			}
			rc = 0;
		} else {
			k_sem_give(&data->lock);
			return rc;
		}
	}

	k_sem_give(&data->lock);

	return rc;
}

static int mr60bha2_channel_get(const struct device *dev, enum sensor_channel chan,
				struct sensor_value *val)
{
	const struct mr60bha2_data *data = dev->data;

	switch ((uint32_t)chan) {
	case SENSOR_CHAN_PROX:
		val->val1 = (data->presence_status != MR60BHA2_PRESENCE_NO_TARGET) ? 1 : 0;
		val->val2 = 0;
		return 0;
	case SENSOR_CHAN_MR60BHA2_PRESENCE_STATUS:
		if (!(data->received_mask & MR60BHA2_MASK_PRESENCE)) {
			return -ENODATA;
		}
		val->val1 = data->presence_status;
		val->val2 = 0;
		return 0;
	case SENSOR_CHAN_MR60BHA2_BREATHING_PHASE:
		val->val1 = data->breathing_phase;
		val->val2 = 0;
		return 0;
	case SENSOR_CHAN_MR60BHA2_BREATHING_RATE:
		if (!(data->received_mask & MR60BHA2_MASK_BREATHING_RATE)) {
			return -ENODATA;
		}
		/* Raw value is tenths of breaths/min; multiply by 100 to get milli-units */
		return sensor_value_from_milli(val, (int64_t)data->breathing_rate_tenths * 100);
	case SENSOR_CHAN_MR60BHA2_HEARTBEAT_PHASE:
		val->val1 = data->heartbeat_phase;
		val->val2 = 0;
		return 0;
	case SENSOR_CHAN_MR60BHA2_HEARTBEAT_RATE:
		if (!(data->received_mask & MR60BHA2_MASK_HEARTBEAT_RATE)) {
			return -ENODATA;
		}
		val->val1 = data->heartbeat_rate_bpm;
		val->val2 = 0;
		return 0;
	default:
		return -ENOTSUP;
	}
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
		LOG_ERR_DEVICE_NOT_READY(config->uart_dev);
		return -ENODEV;
	}

	data->presence_status = MR60BHA2_PRESENCE_NO_TARGET;

	k_sem_init(&data->lock, 1, 1);
	k_sem_init(&data->rx_sem, 0, 1);
	mr60bha2_rx_reset(data);

	uart_irq_rx_disable(config->uart_dev);
	uart_irq_tx_disable(config->uart_dev);

	mr60bha2_uart_flush(config->uart_dev);

	rc = uart_irq_callback_user_data_set(config->uart_dev, config->cb, data);
	if (rc != 0) {
		LOG_ERR("UART IRQ setup failed: %d", rc);
		return rc;
	}

	return 0;
}

#define MR60BHA2_DEFINE(inst)                                                                      \
	static struct mr60bha2_data mr60bha2_data_##inst;                                          \
                                                                                                   \
	static const struct mr60bha2_config mr60bha2_config_##inst = {                             \
		.uart_dev = DEVICE_DT_GET(DT_INST_BUS(inst)),                                      \
		.cb = mr60bha2_uart_isr,                                                           \
	};                                                                                         \
                                                                                                   \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, mr60bha2_init, NULL, &mr60bha2_data_##inst,             \
				     &mr60bha2_config_##inst, POST_KERNEL,                         \
				     CONFIG_SENSOR_INIT_PRIORITY, &mr60bha2_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MR60BHA2_DEFINE)
