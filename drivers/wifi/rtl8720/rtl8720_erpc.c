/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(wifi_rtl8720, CONFIG_WIFI_LOG_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <string.h>

#include "rtl8720.h"

/*
 * The module runs eRPC 1.7.4, whose framed transport prefixes every message
 * with a four byte header holding the payload length and a CRC-16 over the
 * payload, both little endian:
 *
 *   uint16_t message_size;
 *   uint16_t crc;
 *
 * The payload itself starts with the basic codec message header, which packs
 * the codec version, the service, the function and the message type into one
 * word, followed by the sequence number:
 *
 *   uint32_t (version << 24) | (service << 16) | (request << 8) | type
 *   uint32_t sequence
 *
 * Scalars are written in their native little endian representation, binaries
 * and strings as a uint32_t length followed by the bytes.
 */
#define ERPC_FRAME_HEADER_SIZE 4U
#define ERPC_MSG_HEADER_SIZE   8U

#define ERPC_CODEC_VERSION 1U

/*
 * eRPC checks messages with a CRC-16 over poly 0x1021, MSB first and without
 * reflection, which is what crc16_itu_t() computes. Only the seed is its own.
 */
#define ERPC_CRC_SEED 0xEF4AU

#define ERPC_MSG_INVOCATION 0U
#define ERPC_MSG_ONEWAY     1U
#define ERPC_MSG_REPLY      2U
#define ERPC_MSG_NOTIFY     3U

/* Marker the codec writes before a nullable argument */
#define ERPC_NOT_NULL 0U
#define ERPC_IS_NULL  1U

static const struct device *const rtl8720_uart = DEVICE_DT_GET(DT_INST_BUS(0));

void rtl8720_put_u8(struct rtl8720_codec *codec, uint8_t value)
{
	if (codec->err || (codec->pos + sizeof(value)) > codec->size) {
		codec->err = true;
		return;
	}

	codec->data[codec->pos] = value;
	codec->pos += sizeof(value);
}

void rtl8720_put_u16(struct rtl8720_codec *codec, uint16_t value)
{
	if (codec->err || (codec->pos + sizeof(value)) > codec->size) {
		codec->err = true;
		return;
	}

	sys_put_le16(value, &codec->data[codec->pos]);
	codec->pos += sizeof(value);
}

void rtl8720_put_u32(struct rtl8720_codec *codec, uint32_t value)
{
	if (codec->err || (codec->pos + sizeof(value)) > codec->size) {
		codec->err = true;
		return;
	}

	sys_put_le32(value, &codec->data[codec->pos]);
	codec->pos += sizeof(value);
}

void rtl8720_put_i32(struct rtl8720_codec *codec, int32_t value)
{
	rtl8720_put_u32(codec, (uint32_t)value);
}

void rtl8720_put_bin(struct rtl8720_codec *codec, const void *value, size_t len)
{
	rtl8720_put_u32(codec, (uint32_t)len);

	if (codec->err || (codec->pos + len) > codec->size) {
		codec->err = true;
		return;
	}

	memcpy(&codec->data[codec->pos], value, len);
	codec->pos += len;
}

void rtl8720_put_str(struct rtl8720_codec *codec, const char *value)
{
	rtl8720_put_bin(codec, value, strlen(value));
}

void rtl8720_put_nullable_str(struct rtl8720_codec *codec, const char *value)
{
	if (value == NULL) {
		rtl8720_put_u8(codec, ERPC_IS_NULL);
		return;
	}

	rtl8720_put_u8(codec, ERPC_NOT_NULL);
	rtl8720_put_str(codec, value);
}

uint16_t rtl8720_get_u16(struct rtl8720_codec *codec)
{
	uint16_t value;

	if (codec->err || (codec->pos + sizeof(value)) > codec->size) {
		codec->err = true;
		return 0U;
	}

	value = sys_get_le16(&codec->data[codec->pos]);
	codec->pos += sizeof(value);

	return value;
}

uint32_t rtl8720_get_u32(struct rtl8720_codec *codec)
{
	uint32_t value;

	if (codec->err || (codec->pos + sizeof(value)) > codec->size) {
		codec->err = true;
		return 0U;
	}

	value = sys_get_le32(&codec->data[codec->pos]);
	codec->pos += sizeof(value);

	return value;
}

int32_t rtl8720_get_i32(struct rtl8720_codec *codec)
{
	return (int32_t)rtl8720_get_u32(codec);
}

int8_t rtl8720_get_i8(struct rtl8720_codec *codec)
{
	int8_t value;

	if (codec->err || (codec->pos + sizeof(value)) > codec->size) {
		codec->err = true;
		return 0;
	}

	value = (int8_t)codec->data[codec->pos];
	codec->pos += sizeof(value);

	return value;
}

const uint8_t *rtl8720_get_bin(struct rtl8720_codec *codec, size_t *len)
{
	const uint8_t *value;
	uint32_t count;

	count = rtl8720_get_u32(codec);
	if (codec->err || (codec->pos + count) > codec->size) {
		codec->err = true;
		*len = 0U;
		return NULL;
	}

	value = &codec->data[codec->pos];
	codec->pos += count;
	*len = count;

	return value;
}

void rtl8720_erpc_request(struct rtl8720_data *data, struct rtl8720_codec *codec)
{
	codec->data = data->tx_buf;
	codec->size = sizeof(data->tx_buf);
	codec->pos = ERPC_MSG_HEADER_SIZE;
	codec->err = false;
}

static int rtl8720_erpc_send(struct rtl8720_data *data, uint8_t service, uint8_t request,
			     uint32_t sequence, size_t len)
{
	uint8_t header[ERPC_FRAME_HEADER_SIZE];
	uint32_t word;
	size_t i;

	word = ((uint32_t)ERPC_CODEC_VERSION << 24) | ((uint32_t)service << 16) |
	       ((uint32_t)request << 8) | ERPC_MSG_INVOCATION;

	sys_put_le32(word, &data->tx_buf[0]);
	sys_put_le32(sequence, &data->tx_buf[4]);

	sys_put_le16((uint16_t)len, &header[0]);
	sys_put_le16(crc16_itu_t(ERPC_CRC_SEED, data->tx_buf, len), &header[2]);

	for (i = 0U; i < sizeof(header); i++) {
		uart_poll_out(rtl8720_uart, header[i]);
	}

	for (i = 0U; i < len; i++) {
		uart_poll_out(rtl8720_uart, data->tx_buf[i]);
	}

	return 0;
}

int rtl8720_erpc_call(struct rtl8720_data *data, uint8_t service, uint8_t request,
		      struct rtl8720_codec *codec)
{
	size_t len = codec->pos;
	int ret;

	if (codec->err) {
		LOG_ERR("Request to %u/%u does not fit the transmit buffer", service, request);
		return -EIO;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	data->sequence++;
	data->pending_sequence = data->sequence;
	data->reply_valid = false;
	k_sem_reset(&data->reply);

	ret = rtl8720_erpc_send(data, service, request, data->sequence, len);
	if (ret < 0) {
		goto out;
	}

	ret = k_sem_take(&data->reply, K_MSEC(CONFIG_WIFI_RTL8720_REQUEST_TIMEOUT));
	if (ret < 0) {
		LOG_ERR("Timeout waiting for reply to %u/%u", service, request);
		ret = -ETIMEDOUT;
		goto out;
	}

	if (!data->reply_valid) {
		ret = -EIO;
		goto out;
	}

	codec->data = data->rx_buf;
	codec->size = data->rx_len;
	codec->pos = ERPC_MSG_HEADER_SIZE;
	codec->err = false;

out:
	data->pending_sequence = 0U;
	k_mutex_unlock(&data->lock);

	return ret;
}

/*
 * Hand a complete payload to whoever is waiting for it. Anything that is not
 * the reply being waited on is a message the module sent on its own, which
 * this driver does not subscribe to, so it is dropped.
 */
static void rtl8720_erpc_dispatch(struct rtl8720_data *data, const uint8_t *payload, size_t len)
{
	uint32_t word;
	uint32_t sequence;
	uint8_t type;

	if (len < ERPC_MSG_HEADER_SIZE) {
		LOG_WRN("Runt message of %zu bytes", len);
		return;
	}

	word = sys_get_le32(&payload[0]);
	sequence = sys_get_le32(&payload[4]);
	type = (uint8_t)(word & 0xFFU);

	if (((word >> 24) & 0xFFU) != ERPC_CODEC_VERSION) {
		LOG_WRN("Unsupported codec version %u", (word >> 24) & 0xFFU);
		return;
	}

	if (type != ERPC_MSG_REPLY) {
		LOG_DBG("Dropping message of type %u from service %u", type, (word >> 16) & 0xFFU);
		return;
	}

	if (data->pending_sequence == 0U || sequence != data->pending_sequence) {
		LOG_WRN("Reply with sequence %u, expected %u", sequence, data->pending_sequence);
		return;
	}

	if (len > sizeof(data->rx_buf)) {
		LOG_ERR("Reply of %zu bytes exceeds the receive buffer", len);
		data->reply_valid = false;
	} else {
		memcpy(data->rx_buf, payload, len);
		data->rx_len = len;
		data->reply_valid = true;
	}

	k_sem_give(&data->reply);
}

static void rtl8720_uart_isr(const struct device *dev, void *user_data)
{
	struct rtl8720_data *data = user_data;
	uint8_t buf[32];
	int read;

	uart_irq_update(dev);

	while (uart_irq_rx_ready(dev) > 0) {
		read = uart_fifo_read(dev, buf, sizeof(buf));
		if (read <= 0) {
			break;
		}

		if (ring_buf_put(&data->rx_ring, buf, (uint32_t)read) < (uint32_t)read) {
			LOG_WRN("Receive ring buffer overrun");
		}

		k_sem_give(&data->rx_sem);
	}
}

/*
 * Pull exactly @p len bytes out of the ring buffer. A frame that stops short
 * is dropped by the caller, which then resynchronises on the next header.
 */
static int rtl8720_pull(struct rtl8720_data *data, uint8_t *dst, size_t len, k_timeout_t timeout)
{
	k_timepoint_t deadline = sys_timepoint_calc(timeout);
	size_t got = 0U;

	while (got < len) {
		uint32_t read = ring_buf_get(&data->rx_ring, &dst[got], (uint32_t)(len - got));

		if (read > 0U) {
			got += read;
			continue;
		}

		if (k_sem_take(&data->rx_sem, sys_timepoint_timeout(deadline)) < 0) {
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static void rtl8720_rx_thread(void *p1, void *p2, void *p3)
{
	struct rtl8720_data *data = p1;
	static uint8_t payload[CONFIG_WIFI_RTL8720_RX_BUF_SIZE];
	uint8_t header[ERPC_FRAME_HEADER_SIZE];

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		uint16_t len;
		uint16_t crc;

		if (rtl8720_pull(data, header, sizeof(header), K_FOREVER) < 0) {
			continue;
		}

		len = sys_get_le16(&header[0]);
		crc = sys_get_le16(&header[2]);

		if (len == 0U || len > sizeof(payload)) {
			LOG_WRN("Frame of %u bytes out of range, resynchronising", len);
			continue;
		}

		if (rtl8720_pull(data, payload, len, K_MSEC(CONFIG_WIFI_RTL8720_FRAME_TIMEOUT)) <
		    0) {
			LOG_WRN("Truncated frame of %u bytes", len);
			continue;
		}

		if (crc16_itu_t(ERPC_CRC_SEED, payload, len) != crc) {
			LOG_WRN("Frame of %u bytes failed its CRC", len);
			continue;
		}

		rtl8720_erpc_dispatch(data, payload, len);
	}
}

K_KERNEL_STACK_DEFINE(rtl8720_rx_stack, CONFIG_WIFI_RTL8720_RX_STACK_SIZE);
static struct k_thread rtl8720_rx_thread_data;

int rtl8720_erpc_init(const struct device *dev)
{
	struct rtl8720_data *data = dev->data;
	struct rtl8720_codec codec;
	const uint8_t *version;
	size_t version_len;
	unsigned char stale;
	int ret;

	if (!device_is_ready(rtl8720_uart)) {
		LOG_ERR("UART %s not ready", rtl8720_uart->name);
		return -ENODEV;
	}

	ring_buf_init(&data->rx_ring, sizeof(data->rx_ring_buf), data->rx_ring_buf);

	uart_irq_rx_disable(rtl8720_uart);
	uart_irq_tx_disable(rtl8720_uart);
	uart_irq_callback_user_data_set(rtl8720_uart, rtl8720_uart_isr, data);

	/* Drop what the UART latched while the module was held in reset */
	while (uart_poll_in(rtl8720_uart, &stale) == 0) {
	}

	uart_irq_rx_enable(rtl8720_uart);

	k_thread_create(&rtl8720_rx_thread_data, rtl8720_rx_stack,
			K_KERNEL_STACK_SIZEOF(rtl8720_rx_stack), rtl8720_rx_thread, data, NULL,
			NULL, K_PRIO_COOP(CONFIG_WIFI_RTL8720_RX_THREAD_PRIORITY), 0, K_NO_WAIT);
	k_thread_name_set(&rtl8720_rx_thread_data, "rtl8720_rx");

	/*
	 * Ask for the firmware version. It is the cheapest call there is and
	 * it proves the link, the framing and the codec version all at once.
	 */
	rtl8720_erpc_request(data, &codec);

	ret = rtl8720_erpc_call(data, RTL8720_SVC_SYSTEM, RTL8720_SYSTEM_VERSION, &codec);
	if (ret < 0) {
		LOG_ERR("Module did not answer a version request: %d", ret);
		return ret;
	}

	version = rtl8720_get_bin(&codec, &version_len);
	if (codec.err) {
		LOG_ERR("Malformed version reply");
		return -EIO;
	}

	LOG_INF("RTL8720 firmware %.*s", (int)version_len, (const char *)version);

	return 0;
}
