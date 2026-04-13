/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DFRobot AI10 (SEN0677) face recognition module — UART protocol derived from
 * vendor Arduino reference (DFRobot_AI10).
 */

#define DT_DRV_COMPAT dfrobot_ai10

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/biometrics.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <zephyr/drivers/biometrics_dfrobot_ai10.h>

#include "biometrics_dfrobot_ai10.h"

LOG_MODULE_REGISTER(dfrobot_ai10, CONFIG_BIOMETRICS_LOG_LEVEL);

// #define AI10_FACE_UID_MAX 1000U
#define AI10_FACE_UID_MAX 2000U // temp work around to also treat palm as a face

#define AI10_INIT_PROBE_DELAY_MS   1500U
#define AI10_INIT_PROBE_RETRIES    5U
#define AI10_INIT_PROBE_GAP_MS     500U
#define AI10_INIT_PROBE_TIMEOUT_MS 2000U

#define AI10_TIMEOUT_SEC_MIN 3U
#define AI10_TIMEOUT_SEC_MAX 20U

#define AI10_MAX_OP_TIMEOUT_MS 600000U

static const struct uart_config ai10_uart_cfg = {
	.baudrate = 115200,
	.parity = UART_CFG_PARITY_NONE,
	.stop_bits = UART_CFG_STOP_BITS_1,
	.data_bits = UART_CFG_DATA_BITS_8,
	.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
};

static uint32_t ai10_op_timeout_ms(const struct ai10_data *data, k_timeout_t op_timeout)
{
	if (K_TIMEOUT_EQ(op_timeout, K_FOREVER)) {
		return data->timeout_ms;
	}

	if (K_TIMEOUT_EQ(op_timeout, K_NO_WAIT)) {
		return 0U;
	}

	return (uint32_t)MIN(k_ticks_to_ms_ceil64(op_timeout.ticks), (uint64_t)AI10_MAX_OP_TIMEOUT_MS);
}

static uint8_t ai10_xor_buf(const uint8_t *p, uint16_t len)
{
	uint8_t x = 0;

	for (uint16_t i = 0; i < len; i++) {
		x ^= p[i];
	}
	return x;
}

static bool ai10_xor_frame_ok(const uint8_t *frame_after_sync, uint16_t total_after_sync)
{
	uint8_t x = 0;

	if (total_after_sync < 2) {
		return false;
	}

	for (uint16_t i = 0; i < total_after_sync - 1; i++) {
		x ^= frame_after_sync[i];
	}

	return x == frame_after_sync[total_after_sync - 1];
}

static void ai10_uart_tx_handler(const struct device *uart_dev, struct ai10_data *data)
{
	int sent;
	k_spinlock_key_t key = k_spin_lock(&data->irq_lock);
	uint16_t remaining = data->tx_pkt.len - data->tx_pkt.offset;

	if (remaining > 0) {
		sent = uart_fifo_fill(uart_dev, &data->tx_pkt.buf[data->tx_pkt.offset], remaining);
		if (sent > 0) {
			data->tx_pkt.offset += sent;
			remaining = data->tx_pkt.len - data->tx_pkt.offset;
		}
	}

	if (remaining == 0 && uart_irq_tx_complete(uart_dev)) {
		uart_irq_tx_disable(uart_dev);
		k_spin_unlock(&data->irq_lock, key);
		k_sem_give(&data->uart_tx_sem);
		return;
	}

	k_spin_unlock(&data->irq_lock, key);
}

static void ai10_uart_rx_handler(const struct device *uart_dev, struct ai10_data *data)
{
	uint8_t *buf;
	uint32_t space;
	int ret;

	while (uart_irq_rx_ready(uart_dev)) {
		space = ring_buf_put_claim(&data->rx_ring, &buf, AI10_RX_STREAM_BUF_SIZE);
		if (space == 0U) {
			data->rx_error = AI10_RX_OVERFLOW;
			uart_irq_rx_disable(uart_dev);
			k_sem_give(&data->uart_rx_sem);
			return;
		}

		ret = uart_fifo_read(uart_dev, buf, space);
		if (ret < 0) {
			data->rx_error = AI10_RX_OVERFLOW;
			uart_irq_rx_disable(uart_dev);
			k_sem_give(&data->uart_rx_sem);
			return;
		}

		if (ret == 0) {
			ring_buf_put_finish(&data->rx_ring, 0);
			return;
		}

		LOG_HEXDUMP_DBG(buf, ret, "ai10 RX chunk");

		if (ring_buf_put_finish(&data->rx_ring, ret) != 0) {
			data->rx_error = AI10_RX_OVERFLOW;
			uart_irq_rx_disable(uart_dev);
			k_sem_give(&data->uart_rx_sem);
			return;
		}

		k_sem_give(&data->uart_rx_sem);
	}
}

static void ai10_uart_callback(const struct device *uart_dev, void *user_data)
{
	struct ai10_data *data = user_data;

	/*
	 * Match IRQ-driven UART usage in samples/drivers/uart/passthrough and
	 * drivers/bluetooth/hci/h4.c: uart_irq_update() may only refresh cached
	 * status once per call (e.g. NS16550 IIR). After servicing TX, RX may
	 * become visible only on a subsequent update + pending check.
	 */
	while (uart_irq_update(uart_dev) > 0 && uart_irq_is_pending(uart_dev) > 0) {
		if (uart_irq_tx_ready(uart_dev)) {
			ai10_uart_tx_handler(uart_dev, data);
		}

		if (uart_irq_rx_ready(uart_dev)) {
			ai10_uart_rx_handler(uart_dev, data);
		}
	}
}

static int ai10_send_frame(const struct device *dev, const uint8_t *payload, uint16_t payload_len)
{
	const struct ai10_config *cfg = dev->config;
	struct ai10_data *data = dev->data;
	uint16_t total = 2U + payload_len + 1U;
	k_spinlock_key_t key;

	if (payload_len > (AI10_MAX_FRAME - 3U)) {
		return -EINVAL;
	}

	data->tx_pkt.buf[0] = AI10_SYNC_H;
	data->tx_pkt.buf[1] = AI10_SYNC_L;
	memcpy(&data->tx_pkt.buf[2], payload, payload_len);
	data->tx_pkt.buf[2 + payload_len] = ai10_xor_buf(payload, payload_len);

	key = k_spin_lock(&data->irq_lock);
	data->tx_pkt.len = total;
	data->tx_pkt.offset = 0;
	k_spin_unlock(&data->irq_lock, key);

	LOG_HEXDUMP_DBG(data->tx_pkt.buf, total, "ai10 TX");

	uart_irq_tx_enable(cfg->uart_dev);

	if (k_sem_take(&data->uart_tx_sem, K_MSEC(AI10_UART_CHUNK_TIMEOUT_MS)) != 0) {
		uart_irq_tx_disable(cfg->uart_dev);
		LOG_ERR("UART TX timeout");
		return -ETIMEDOUT;
	}

	return 0;
}

static void ai10_rx_prepare(const struct device *dev)
{
	const struct ai10_config *cfg = dev->config;
	struct ai10_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->irq_lock);
	uint8_t discard[16];

	uart_irq_rx_disable(cfg->uart_dev);

	/* Drain stale bytes from the hardware FIFO so they do not contaminate
	 * the next receive.  Late responses or sensor boot messages can leave
	 * bytes sitting in the FIFO between transactions.
	 */
	while (uart_fifo_read(cfg->uart_dev, discard, sizeof(discard)) > 0) {
	}

	ring_buf_reset(&data->rx_ring);
	data->rx_pkt.len = 0;
	data->rx_expected = 5U;
	data->rx_error = AI10_RX_OK;
	k_sem_reset(&data->uart_rx_sem);
	k_spin_unlock(&data->irq_lock, key);
}

static int ai10_rx_try_parse(struct ai10_data *data)
{
	uint8_t byte;
	uint16_t offset;

	while (ring_buf_get(&data->rx_ring, &byte, 1) == 1) {
		offset = data->rx_pkt.len;

		if (offset >= AI10_MAX_FRAME) {
			data->rx_error = AI10_RX_OVERFLOW;
			data->rx_pkt.len = 0;
			return -EIO;
		}

		data->rx_pkt.buf[offset++] = byte;

		if (offset == 1U && data->rx_pkt.buf[0] != AI10_SYNC_H) {
			data->rx_pkt.len = 0;
			continue;
		}

		if (offset == 2U && data->rx_pkt.buf[1] != AI10_SYNC_L) {
			if (data->rx_pkt.buf[1] == AI10_SYNC_H) {
				data->rx_pkt.buf[0] = AI10_SYNC_H;
				data->rx_pkt.len = 1U;
			} else {
				data->rx_pkt.len = 0;
			}
			continue;
		}

		if (offset < 5U) {
			data->rx_pkt.len = offset;
			continue;
		}

		if (offset == 5U) {
			uint16_t plen = sys_get_be16(&data->rx_pkt.buf[3]);

			if (plen > AI10_MAX_PAYLOAD) {
				data->rx_error = AI10_RX_BAD_LEN;
				data->rx_pkt.len = 0;
				return -EBADMSG;
			}
			data->rx_expected = 6U + plen;
		}

		data->rx_pkt.len = offset;
		if (offset >= data->rx_expected) {
			return 1;
		}
	}

	return 0;
}

static int ai10_recv_frame_body(const struct device *dev, uint32_t timeout_ms)
{
	struct ai10_data *data = dev->data;
	int64_t deadline = k_uptime_get() + (int64_t)timeout_ms;
	int64_t remain;
	uint32_t wait_ms;
	int ret;

	for (;;) {
		if (data->rx_error != AI10_RX_OK) {
			LOG_ERR("UART RX error %d", data->rx_error);
			return -EIO;
		}

		ret = ai10_rx_try_parse(data);
		if (ret < 0) {
			return ret;
		}

		if (ret > 0) {
			break;
		}

		if (timeout_ms == 0U) {
			LOG_ERR("UART RX timeout");
			return -ETIMEDOUT;
		}

		remain = deadline - k_uptime_get();
		if (remain <= 0) {
			LOG_ERR("UART RX timeout");
			return -ETIMEDOUT;
		}

		wait_ms = (uint32_t)MIN((uint64_t)remain, (uint64_t)UINT32_MAX);
		if (wait_ms == 0U) {
			wait_ms = 1U;
		}

		if (k_sem_take(&data->uart_rx_sem, K_MSEC(wait_ms)) != 0) {
			LOG_ERR("UART RX timeout");
			return -ETIMEDOUT;
		}
	}

	LOG_HEXDUMP_DBG(data->rx_pkt.buf, data->rx_pkt.len, "ai10 RX");

	if (data->rx_pkt.len < 6U) {
		return -EBADMSG;
	}

	if (!ai10_xor_frame_ok(&data->rx_pkt.buf[2], data->rx_pkt.len - 2U)) {
		LOG_ERR("XOR checksum mismatch");
		return -EBADMSG;
	}

	return 0;
}

struct ai10_parsed_msg {
	uint8_t transport_mid;
	uint8_t inner_mid;
	uint8_t result;
	const uint8_t *inner_data;
	uint16_t inner_data_len;
};

static int ai10_parse_relay(const struct device *dev, struct ai10_parsed_msg *out)
{
	struct ai10_data *data = dev->data;
	uint16_t plen;

	if (data->rx_pkt.len < 6U) {
		return -EBADMSG;
	}

	out->transport_mid = data->rx_pkt.buf[2];
	plen = sys_get_be16(&data->rx_pkt.buf[3]);

	if (out->transport_mid != AI10_MID_RELAY) {
		return -EBADMSG;
	}

	if (plen > AI10_MAX_PAYLOAD || (uint32_t)plen + 6U > data->rx_pkt.len) {
		return -EBADMSG;
	}

	/* Inner frame: at least mid + result + optional data (vendor layout) */
	if (plen < 2U) {
		return -EBADMSG;
	}

	out->inner_mid = data->rx_pkt.buf[5];
	out->result = data->rx_pkt.buf[6];
	out->inner_data = (plen > 2U) ? &data->rx_pkt.buf[7] : NULL;
	out->inner_data_len = (plen > 2U) ? (plen - 2U) : 0U;

	return 0;
}

static int ai10_recv_until_relay(const struct device *dev, uint32_t timeout_ms,
				 struct ai10_parsed_msg *out, bool rx_already_armed)
{
	const struct ai10_config *cfg = dev->config;
	struct ai10_data *data = dev->data;
	int64_t deadline = k_uptime_get() + (int64_t)timeout_ms;

	/*
	 * Arm RX once at entry.  Between frames we only reset the packet parse
	 * state — the ring buffer is left intact so bytes the ISR deposited
	 * while we were processing a NOTE frame are not lost.
	 */
	if (!rx_already_armed) {
		ai10_rx_prepare(dev);
		uart_irq_rx_enable(cfg->uart_dev);
	}

	for (;;) {
		int64_t remain = deadline - k_uptime_get();
		uint32_t chunk_ms;
		int ret;

		if (remain <= 0) {
			return -ETIMEDOUT;
		}

		chunk_ms = (uint32_t)MIN((uint64_t)remain, (uint64_t)UINT32_MAX);
		if (chunk_ms == 0U) {
			chunk_ms = 1U;
		}

		ret = ai10_recv_frame_body(dev, chunk_ms);
		if (ret < 0) {
			return ret;
		}

		if (data->rx_pkt.len < 5U) {
			return -EBADMSG;
		}

		if (data->rx_pkt.buf[2] == AI10_MID_NOTE) {
			/* Reset packet state for next frame, ring buffer stays intact */
			data->rx_pkt.len = 0;
			data->rx_expected = 5U;
			continue;
		}

		if (data->rx_pkt.buf[2] == AI10_MID_RELAY) {
			return ai10_parse_relay(dev, out);
		}

		return -EBADMSG;
	}
}

static int ai10_send_recv(const struct device *dev, const uint8_t *payload, uint16_t payload_len,
			  uint32_t timeout_ms, struct ai10_parsed_msg *out)
{
	const struct ai10_config *cfg = dev->config;
	int ret;

	/*
	 * Arm RX before TX: the module can begin its reply within microseconds of
	 * the last TX byte. Enabling RX after bytes are already in the FIFO may
	 * not re-trigger the IRQ on some UART drivers, causing RX timeouts.
	 */
	ai10_rx_prepare(dev);
	uart_irq_rx_enable(cfg->uart_dev);

	ret = ai10_send_frame(dev, payload, payload_len);
	if (ret < 0) {
		uart_irq_rx_disable(cfg->uart_dev);
		return ret;
	}

	ret = ai10_recv_until_relay(dev, timeout_ms, out, true);
	uart_irq_rx_disable(cfg->uart_dev);
	return ret;
}

static int ai10_probe(const struct device *dev)
{
	static const uint8_t payload[] = {AI10_CMD_GET_STATUS, 0x00, 0x00};
	struct ai10_parsed_msg msg;
	int ret = -ETIMEDOUT;
	uint32_t attempt;

	for (attempt = 0U; attempt < AI10_INIT_PROBE_RETRIES; attempt++) {
		ret = ai10_send_recv(dev, payload, sizeof(payload), AI10_INIT_PROBE_TIMEOUT_MS,
				     &msg);
		if (ret == 0) {
			if (msg.inner_mid != AI10_CMD_GET_STATUS) {
				return -EIO;
			}

			return 0;
		}

		LOG_DBG("AI10 status probe attempt %u/%u failed: %d", attempt + 1U,
			AI10_INIT_PROBE_RETRIES, ret);
		k_msleep(AI10_INIT_PROBE_GAP_MS);
	}

	return ret;
}

static void ai10_enroll_opts_clear(struct ai10_data *data)
{
	data->enroll_opts_valid = false;
}

static int ai10_result_to_errno(uint8_t result)
{
	switch (result) {
	case AI10_RESULT_SUCCESS:
		return 0;
	case AI10_RESULT_FAILED_UNKNOWN_USER:
		return -ENOENT;
	case AI10_RESULT_FAILED_MAX_USER:
		return -ENOSPC;
	case AI10_RESULT_FAILED_FACE_ENROLLED:
		return -EEXIST;
	case AI10_RESULT_FAILED_TIMEOUT:
		return -ETIMEDOUT;
	case AI10_RESULT_FAILED_CAMERA:
		return -EIO;
	default:
		return -EIO;
	}
}

static uint8_t ai10_timeout_sec(const struct ai10_data *data, k_timeout_t op_timeout)
{
	uint64_t ms;

	/*
	 * Seconds sent to the module (enroll / VERIFY). Must follow the API
	 * operation timeout (e.g. biometric_enroll_capture(..., K_SECONDS(30))),
	 * not a separate smaller cap — the old MIN(..., data->timeout_ms) forced
	 * 5 s on the wire when Kconfig default was 5000 ms even if the caller
	 * passed a 30 s k_timeout_t, causing spurious 0x0d (eFailedTimeout).
	 */
	ms = ai10_op_timeout_ms(data, op_timeout);
	if (ms == 0U && !K_TIMEOUT_EQ(op_timeout, K_NO_WAIT)) {
		ms = 1U;
	}

	return (uint8_t)CLAMP(ms / 1000U, AI10_TIMEOUT_SEC_MIN, AI10_TIMEOUT_SEC_MAX);
}

static int ai10_get_capabilities(const struct device *dev, struct biometric_capabilities *caps)
{
	ARG_UNUSED(dev);

	caps->type = BIOMETRIC_TYPE_FACE;
	caps->max_templates = CONFIG_BIOMETRICS_DFROBOT_AI10_MAX_TEMPLATES;
	caps->template_size = 0U;
	caps->storage_modes = BIOMETRIC_STORAGE_DEVICE;
	caps->enrollment_samples_required = 1U;

	return 0;
}

static int ai10_attr_set(const struct device *dev, enum biometric_attribute attr, int32_t val)
{
	struct ai10_data *data = dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	switch (attr) {
	case BIOMETRIC_ATTR_TIMEOUT_MS:
		if (val < 1000 || val > 600000) {
			ret = -EINVAL;
		} else {
			data->timeout_ms = (uint32_t)val;
		}
		break;
	default:
		ret = -ENOTSUP;
		break;
	}

	k_mutex_unlock(&data->lock);
	return ret;
}

static int ai10_attr_get(const struct device *dev, enum biometric_attribute attr, int32_t *val)
{
	struct ai10_data *data = dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	switch (attr) {
	case BIOMETRIC_ATTR_TIMEOUT_MS:
		*val = (int32_t)data->timeout_ms;
		break;
	case BIOMETRIC_ATTR_IMAGE_QUALITY:
		*val = data->last_image_quality;
		break;
	default:
		ret = -ENOTSUP;
		break;
	}

	k_mutex_unlock(&data->lock);
	return ret;
}

static int ai10_enroll_start(const struct device *dev, uint16_t template_id)
{
	struct ai10_data *data = dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->enroll_active) {
		ret = -EBUSY;
		goto unlock;
	}

	if (template_id == 0U || template_id > CONFIG_BIOMETRICS_DFROBOT_AI10_MAX_TEMPLATES) {
		ret = -EINVAL;
		goto unlock;
	}

	data->enroll_active = true;
	data->enroll_template_id = template_id;

unlock:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int ai10_enroll_capture(const struct device *dev, k_timeout_t timeout,
			       struct biometric_capture_result *result)
{
	struct ai10_data *data = dev->data;
	uint8_t enroll_buf[40];
	uint8_t sec;
	struct ai10_parsed_msg msg;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (!data->enroll_active) {
		k_mutex_unlock(&data->lock);
		return -EINVAL;
	}

	sec = ai10_timeout_sec(data, timeout);

	/* ENROLL_SINGLE + sizeof(enrollData) + enrollData */
	enroll_buf[0] = AI10_CMD_ENROLL_SINGLE;
	enroll_buf[1] = (uint8_t)((AI10_ENROLL_DATA_SIZE >> 8) & 0xFF);
	enroll_buf[2] = (uint8_t)(AI10_ENROLL_DATA_SIZE & 0xFF);

	if (data->enroll_opts_valid) {
		enroll_buf[3] = data->enroll_is_admin ? AI10_ADMIN_ADMIN : AI10_ADMIN_NORMAL;
		memset(&enroll_buf[4], 0, 32);
		if (data->enroll_custom_name[0] == '\0') {
			snprintk((char *)&enroll_buf[4], 32, "u%04u", data->enroll_template_id);
		} else {
			memcpy(&enroll_buf[4], data->enroll_custom_name, 32);
		}
	} else {
		enroll_buf[3] = AI10_ADMIN_NORMAL;
		memset(&enroll_buf[4], 0, 32);
		snprintk((char *)&enroll_buf[4], 32, "u%04u", data->enroll_template_id);
	}
	enroll_buf[36] = AI10_FACE_DIR_UNDEF;
	enroll_buf[37] = sec;

	ret = ai10_send_recv(dev, enroll_buf, 3U + AI10_ENROLL_DATA_SIZE,
			     ai10_op_timeout_ms(data, timeout), &msg);
	if (ret < 0) {
		ai10_enroll_opts_clear(data);
		data->enroll_active = false;
		k_mutex_unlock(&data->lock);
		return ret;
	}

	ret = ai10_result_to_errno(msg.result);
	if (ret < 0) {
		LOG_WRN("AI10 enrollment failed: module result 0x%02x (wire timeout was %u s)",
			msg.result, (unsigned int)sec);
		ai10_enroll_opts_clear(data);
		data->enroll_active = false;
		k_mutex_unlock(&data->lock);
		return ret;
	}

	data->last_image_quality = 85;
	data->enroll_active = false;
	ai10_enroll_opts_clear(data);

	if (result != NULL) {
		result->samples_captured = 1U;
		result->samples_required = 1U;
		result->quality = 85U;
	}

	k_mutex_unlock(&data->lock);
	return 0;
}

static int ai10_enroll_finalize(const struct device *dev)
{
	struct ai10_data *data = dev->data;

	k_mutex_lock(&data->lock, K_FOREVER);

	/* Single-shot enroll completes in enroll_capture */
	if (data->enroll_active) {
		k_mutex_unlock(&data->lock);
		return -EINVAL;
	}

	k_mutex_unlock(&data->lock);
	return 0;
}

static int ai10_enroll_abort(const struct device *dev)
{
	struct ai10_data *data = dev->data;

	k_mutex_lock(&data->lock, K_FOREVER);
	ai10_enroll_opts_clear(data);
	data->enroll_active = false;
	k_mutex_unlock(&data->lock);

	return 0;
}

static int ai10_template_delete(const struct device *dev, uint16_t id)
{
	uint8_t payload[] = {
		AI10_CMD_DEL_USER,
		0x00,
		0x02,
		(uint8_t)((id >> 8) & 0xFF),
		(uint8_t)(id & 0xFF),
	};
	struct ai10_data *data = dev->data;
	struct ai10_parsed_msg msg;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	ret = ai10_send_recv(dev, payload, sizeof(payload), data->timeout_ms, &msg);
	if (ret < 0) {
		k_mutex_unlock(&data->lock);
		return ret;
	}

	ret = ai10_result_to_errno(msg.result);
	k_mutex_unlock(&data->lock);
	return ret;
}

static int ai10_template_delete_all(const struct device *dev)
{
	static const uint8_t payload[] = { AI10_CMD_DEL_ALL_USER, 0x00, 0x00 };
	struct ai10_data *data = dev->data;
	struct ai10_parsed_msg msg;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	ret = ai10_send_recv(dev, payload, sizeof(payload), data->timeout_ms, &msg);
	if (ret < 0) {
		k_mutex_unlock(&data->lock);
		return ret;
	}

	ret = ai10_result_to_errno(msg.result);
	k_mutex_unlock(&data->lock);
	return ret;
}

static int ai10_template_list(const struct device *dev, uint16_t *ids, size_t max_count,
			      size_t *actual_count)
{
	static const uint8_t payload[] = { AI10_CMD_GET_ALL_USERID, 0x00, 0x01, 0x00 };
	struct ai10_data *data = dev->data;
	struct ai10_parsed_msg msg;
	int ret;
	uint8_t n;
	size_t i;

	k_mutex_lock(&data->lock, K_FOREVER);

	ret = ai10_send_recv(dev, payload, sizeof(payload), data->timeout_ms, &msg);
	if (ret < 0) {
		k_mutex_unlock(&data->lock);
		return ret;
	}

	ret = ai10_result_to_errno(msg.result);
	if (ret < 0) {
		k_mutex_unlock(&data->lock);
		return ret;
	}

	if (msg.inner_data == NULL || msg.inner_data_len < 1U) {
		*actual_count = 0U;
		k_mutex_unlock(&data->lock);
		return 0;
	}

	n = msg.inner_data[0];
	*actual_count = MIN((size_t)n, max_count);

	for (i = 0; i < *actual_count; i++) {
		if (2U + i * 2U >= msg.inner_data_len) {
			break;
		}
		ids[i] = sys_get_be16(&msg.inner_data[1 + i * 2]);
	}
	*actual_count = i;

	k_mutex_unlock(&data->lock);
	return 0;
}

static int ai10_match(const struct device *dev, enum biometric_match_mode mode,
		      uint16_t template_id, k_timeout_t timeout,
		      struct biometric_match_result *result)
{
	struct ai10_data *data = dev->data;
	uint8_t payload[] = {
		AI10_CMD_VERIFY,
		0x00,
		0x02,
		0x00,
		0x00,
	};
	struct ai10_parsed_msg msg;
	uint8_t sec = ai10_timeout_sec(data, timeout);
	uint16_t uid;
	int ret;

	payload[4] = sec;

	k_mutex_lock(&data->lock, K_FOREVER);

	ret = ai10_send_recv(dev, payload, sizeof(payload), ai10_op_timeout_ms(data, timeout),
			     &msg);
	if (ret < 0) {
		k_mutex_unlock(&data->lock);
		return ret;
	}

	if (msg.inner_mid != AI10_CMD_VERIFY) {
		k_mutex_unlock(&data->lock);
		return -EIO;
	}

	ret = ai10_result_to_errno(msg.result);
	if (ret < 0) {
		k_mutex_unlock(&data->lock);
		return ret;
	}

	if (msg.inner_data_len < 2U) {
		k_mutex_unlock(&data->lock);
		return -EBADMSG;
	}

	uid = sys_get_be16(msg.inner_data);
	if (uid > AI10_FACE_UID_MAX) {
		k_mutex_unlock(&data->lock);
		return -ENOENT;
	}

	if (mode == BIOMETRIC_MATCH_VERIFY && uid != template_id) {
		k_mutex_unlock(&data->lock);
		return -ENOENT;
	}

	data->last_image_quality = 90;

	if (result != NULL) {
		result->confidence = 100;
		result->template_id = uid;
		result->image_quality = 90U;
	}

	k_mutex_unlock(&data->lock);
	return 0;
}

int dfrobot_ai10_user_info_get(const struct device *dev, uint16_t uid,
			       struct dfrobot_ai10_user_info *info)
{
	uint8_t payload[5] = {
		AI10_CMD_GET_USER_INFO,
		0x00,
		0x02,
		(uint8_t)((uid >> 8) & 0xFF),
		(uint8_t)(uid & 0xFF),
	};
	struct ai10_data *data;
	struct ai10_parsed_msg msg;
	int ret;
	size_t i;

	if (dev == NULL || info == NULL) {
		return -EINVAL;
	}

	if (!device_is_ready(dev)) {
		return -ENODEV;
	}

	if (!DEVICE_API_IS(biometric, dev) || dev->api != &biometrics_dfrobot_ai10_api) {
		return -ENOTSUP;
	}

	data = dev->data;

	k_mutex_lock(&data->lock, K_FOREVER);

	ret = ai10_send_recv(dev, payload, sizeof(payload), data->timeout_ms, &msg);
	if (ret < 0) {
		k_mutex_unlock(&data->lock);
		return ret;
	}

	if (msg.inner_mid != AI10_CMD_GET_USER_INFO) {
		k_mutex_unlock(&data->lock);
		return -EIO;
	}

	ret = ai10_result_to_errno(msg.result);
	if (ret < 0) {
		k_mutex_unlock(&data->lock);
		return ret;
	}

	if (msg.inner_data == NULL || msg.inner_data_len < 35U) {
		k_mutex_unlock(&data->lock);
		return -EBADMSG;
	}

	info->uid = sys_get_be16(msg.inner_data);
	memcpy(info->user_name, &msg.inner_data[2], sizeof(info->user_name));
	for (i = 0; i < sizeof(info->user_name); i++) {
		if (info->user_name[i] == '\0') {
			break;
		}
	}
	if (i == sizeof(info->user_name)) {
		info->user_name[sizeof(info->user_name) - 1U] = '\0';
	}

	info->is_admin = (msg.inner_data[34] != 0U);

	k_mutex_unlock(&data->lock);
	return 0;
}

int dfrobot_ai10_enroll_options_set(const struct device *dev,
				    const struct dfrobot_ai10_enroll_options *opts)
{
	struct ai10_data *data;

	if (dev == NULL || opts == NULL) {
		return -EINVAL;
	}

	if (!DEVICE_API_IS(biometric, dev) || dev->api != &biometrics_dfrobot_ai10_api) {
		return -ENOTSUP;
	}

	data = dev->data;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (!opts->use_custom) {
		data->enroll_opts_valid = false;
	} else {
		data->enroll_opts_valid = true;
		data->enroll_is_admin = opts->is_admin;
		memcpy(data->enroll_custom_name, opts->user_name, sizeof(data->enroll_custom_name));
	}

	k_mutex_unlock(&data->lock);
	return 0;
}

int dfrobot_ai10_enroll_options_clear(const struct device *dev)
{
	struct ai10_data *data;

	if (dev == NULL) {
		return -EINVAL;
	}

	if (!DEVICE_API_IS(biometric, dev) || dev->api != &biometrics_dfrobot_ai10_api) {
		return -ENOTSUP;
	}

	data = dev->data;

	k_mutex_lock(&data->lock, K_FOREVER);
	data->enroll_opts_valid = false;
	k_mutex_unlock(&data->lock);
	return 0;
}

DEVICE_API(biometric, biometrics_dfrobot_ai10_api) = {
	.get_capabilities = ai10_get_capabilities,
	.attr_set = ai10_attr_set,
	.attr_get = ai10_attr_get,
	.enroll_start = ai10_enroll_start,
	.enroll_capture = ai10_enroll_capture,
	.enroll_finalize = ai10_enroll_finalize,
	.enroll_abort = ai10_enroll_abort,
	.template_store = NULL,
	.template_read = NULL,
	.template_delete = ai10_template_delete,
	.template_delete_all = ai10_template_delete_all,
	.template_list = ai10_template_list,
	.match = ai10_match,
	.led_control = NULL,
};

static int dfrobot_ai10_init(const struct device *dev)
{
	const struct ai10_config *cfg = dev->config;
	struct ai10_data *data = dev->data;
	int ret;

	if (!device_is_ready(cfg->uart_dev)) {
		LOG_ERR("UART not ready");
		return -ENODEV;
	}

	ret = uart_configure(cfg->uart_dev, &ai10_uart_cfg);
	if (ret < 0) {
		if (ret == -ENOSYS) {
			LOG_ERR("UART does not support runtime configure");
		} else {
			LOG_ERR("UART configure failed: %d", ret);
		}
		return ret;
	}

	data->dev = dev;
	data->enroll_active = false;
	data->enroll_opts_valid = false;
	data->timeout_ms = CONFIG_BIOMETRICS_DFROBOT_AI10_TIMEOUT_MS;
	data->last_image_quality = 0;

	k_mutex_init(&data->lock);
	k_sem_init(&data->uart_tx_sem, 0, 1);
	k_sem_init(&data->uart_rx_sem, 0, 1);
	ring_buf_init(&data->rx_ring, sizeof(data->rx_ring_buf), data->rx_ring_buf);

	ret = uart_irq_callback_user_data_set(cfg->uart_dev, ai10_uart_callback, data);
	if (ret < 0) {
		if (ret == -ENOTSUP) {
			LOG_ERR("Interrupt-driven UART API support not enabled");
		} else if (ret == -ENOSYS) {
			LOG_ERR("UART device does not support interrupt-driven API");
		} else {
			LOG_ERR("UART callback setup failed: %d", ret);
		}
		return ret;
	}

	uart_irq_rx_disable(cfg->uart_dev);
	uart_irq_tx_disable(cfg->uart_dev);

	LOG_DBG("AI10 bound to UART %s at %u baud", cfg->uart_dev->name, ai10_uart_cfg.baudrate);

	/*
	 * The real module needs time to finish booting its camera / vision stack.
	 * Probing with RESET during that window is brittle and resetting before
	 * every command is unnecessarily disruptive. Wait briefly, then use a
	 * cheap GET_STATUS transaction as the initial liveness check.
	 */
	k_msleep(AI10_INIT_PROBE_DELAY_MS);

	if (ai10_probe(dev) != 0) {
		LOG_WRN("AI10 status probe failed (check wiring, baud 115200, and sensor boot "
			"time)");
	}

	return 0;
}

#define AI10_DEFINE(inst)                                                                        \
	static struct ai10_data ai10_data_##inst;                                              \
                                                                                                   \
	static const struct ai10_config ai10_cfg_##inst = {                                    \
		.uart_dev = DEVICE_DT_GET(DT_INST_BUS(inst)),                                      \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, dfrobot_ai10_init, NULL, &ai10_data_##inst, &ai10_cfg_##inst, \
			      POST_KERNEL, CONFIG_BIOMETRICS_INIT_PRIORITY,                        \
			      &biometrics_dfrobot_ai10_api);

DT_INST_FOREACH_STATUS_OKAY(AI10_DEFINE)
