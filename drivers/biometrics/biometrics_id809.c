/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Siratul Islam
 */

#define DT_DRV_COMPAT dfrobot_id809

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/biometrics.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>

#include "biometrics_id809.h"

LOG_MODULE_REGISTER(id809, CONFIG_BIOMETRICS_LOG_LEVEL);

/* Convert ID809 return codes to errno */
static int id809_ret_to_errno(uint16_t ret)
{
	switch (ret) {
	case ID809_RET_SUCCESS:
		return 0;
	case ID809_RET_FP_NOT_DETECT:
		return -EAGAIN;
	case ID809_RET_VERIFY_FAIL:
	case ID809_RET_IDENTIFY_FAIL:
	case ID809_RET_TMPL_EMPTY:
	case ID809_RET_ALL_TMPL_EMPTY:
		return -ENOENT;
	case ID809_RET_TMPL_NOT_EMPTY:
		return -EEXIST;
	case ID809_RET_TIMEOUT:
	case ID809_RET_RECV_TIMEOUT:
		return -ETIMEDOUT;
	case ID809_RET_NOT_AUTHORIZED:
		return -EACCES;
	case ID809_RET_INVALID_PARAM:
	case ID809_RET_INVALID_TMPL:
		return -EINVAL;
	case ID809_RET_BAD_QUALITY:
	case ID809_RET_MERGE_FAIL:
	case ID809_RET_MEMORY_ERR:
	case ID809_RET_GATHER_OUT:
	case ID809_RET_FAIL:
	default:
		return -EIO;
	}
}

/* UART IRQ handlers */
static void id809_uart_tx_handler(const struct device *uart_dev, struct id809_data *data)
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

static void id809_uart_rx_handler(const struct device *uart_dev, struct id809_data *data)
{
	uint8_t byte;
	uint16_t offset;
	uint16_t expected;
	k_spinlock_key_t key;

	while (uart_fifo_read(uart_dev, &byte, 1) > 0) {
		key = k_spin_lock(&data->irq_lock);
		offset = data->rx_pkt.len;

		if (offset >= ID809_MAX_PKT_SIZE) {
			data->rx_error = ID809_RX_OVERFLOW;
			uart_irq_rx_disable(uart_dev);
			k_spin_unlock(&data->irq_lock, key);
			k_sem_give(&data->uart_rx_sem);
			return;
		}

		data->rx_pkt.buf[offset++] = byte;

		/*
		 * After receiving the full extended header (10 bytes: prefix,
		 * SID, DID, RCM, LEN, RET) we can determine the packet type
		 * and total expected length.
		 */
		if (offset == ID809_EXTHDR_SIZE) {
			uint8_t b0 = data->rx_pkt.buf[0];
			uint8_t b1 = data->rx_pkt.buf[1];

			if (b0 == ID809_RCM_PREFIX_B0 && b1 == ID809_RCM_PREFIX_B1) {
				/* ACK response: fixed 26-byte packet */
				data->rx_expected = ID809_ACK_PKT_SIZE;
			} else if (b0 == ID809_DAT_PREFIX_B0 && b1 == ID809_DAT_PREFIX_B1) {
				/* Data response: 10 + LEN bytes */
				uint16_t len = sys_get_le16(&data->rx_pkt.buf[ID809_OFF_LEN]);
				uint16_t total = ID809_EXTHDR_SIZE + len;

				if (total > ID809_MAX_PKT_SIZE) {
					data->rx_error = ID809_RX_INVALID_LEN;
					uart_irq_rx_disable(uart_dev);
					k_spin_unlock(&data->irq_lock, key);
					k_sem_give(&data->uart_rx_sem);
					return;
				}
				data->rx_expected = total;
			} else {
				data->rx_error = ID809_RX_OVERFLOW;
				uart_irq_rx_disable(uart_dev);
				k_spin_unlock(&data->irq_lock, key);
				k_sem_give(&data->uart_rx_sem);
				return;
			}
		}

		expected = data->rx_expected;
		data->rx_pkt.len = offset;

		if (offset >= ID809_EXTHDR_SIZE && offset >= expected) {
			uart_irq_rx_disable(uart_dev);
			k_spin_unlock(&data->irq_lock, key);
			k_sem_give(&data->uart_rx_sem);
			return;
		}

		k_spin_unlock(&data->irq_lock, key);
	}
}

static void id809_uart_callback(const struct device *uart_dev, void *user_data)
{
	struct id809_data *data = user_data;

	if (!uart_irq_update(uart_dev)) {
		return;
	}

	if (uart_irq_tx_ready(uart_dev)) {
		id809_uart_tx_handler(uart_dev, data);
	}

	if (uart_irq_rx_ready(uart_dev)) {
		id809_uart_rx_handler(uart_dev, data);
	}
}

/* Build and transmit a 26-byte command packet */
static int id809_send_cmd(const struct device *dev, uint16_t cmd, const uint8_t *payload,
			  uint16_t payload_len)
{
	const struct id809_config *cfg = dev->config;
	struct id809_data *data = dev->data;
	uint16_t cks = 0;
	k_spinlock_key_t key;

	if (payload_len > ID809_CMD_PAD_SIZE) {
		return -ENOMEM;
	}

	/* Fixed 26-byte command packet layout */
	data->tx_pkt.buf[0] = ID809_CMD_PREFIX_B0;
	data->tx_pkt.buf[1] = ID809_CMD_PREFIX_B1;
	data->tx_pkt.buf[ID809_OFF_SID] = ID809_SID_HOST;
	data->tx_pkt.buf[ID809_OFF_DID] = ID809_DID_DEV;
	sys_put_le16(cmd, &data->tx_pkt.buf[ID809_OFF_CMD]);
	sys_put_le16(payload_len, &data->tx_pkt.buf[ID809_OFF_LEN]);

	/* Payload area: copy payload and zero-pad the rest */
	if (payload_len > 0) {
		memcpy(&data->tx_pkt.buf[ID809_OFF_PAY], payload, payload_len);
	}
	memset(&data->tx_pkt.buf[ID809_OFF_PAY + payload_len], 0,
	       ID809_CMD_PAD_SIZE - payload_len);

	/* CKS = sum of bytes [2..23] stored LE in [24..25] */
	for (uint8_t i = ID809_OFF_SID; i < ID809_OFF_PAY + ID809_CMD_PAD_SIZE; i++) {
		cks += data->tx_pkt.buf[i];
	}
	sys_put_le16(cks, &data->tx_pkt.buf[ID809_CMD_PKT_SIZE - ID809_CKS_SIZE]);

	key = k_spin_lock(&data->irq_lock);
	data->tx_pkt.len = ID809_CMD_PKT_SIZE;
	data->tx_pkt.offset = 0;
	k_spin_unlock(&data->irq_lock, key);

	LOG_HEXDUMP_DBG(data->tx_pkt.buf, ID809_CMD_PKT_SIZE, "TX");

	uart_irq_tx_enable(cfg->uart_dev);

	if (k_sem_take(&data->uart_tx_sem, K_MSEC(ID809_UART_TIMEOUT_MS)) != 0) {
		uart_irq_tx_disable(cfg->uart_dev);
		LOG_ERR("UART TX timeout");
		return -ETIMEDOUT;
	}

	return 0;
}

/* Receive and validate a response packet */
static int id809_recv_pkt(const struct device *dev)
{
	const struct id809_config *cfg = dev->config;
	struct id809_data *data = dev->data;
	uint16_t recv_cks, calc_cks;
	uint16_t total_len;
	k_spinlock_key_t key;

	key = k_spin_lock(&data->irq_lock);
	data->rx_pkt.len = 0;
	data->rx_expected = ID809_EXTHDR_SIZE;
	data->rx_error = ID809_RX_OK;
	k_spin_unlock(&data->irq_lock, key);

	uart_irq_rx_enable(cfg->uart_dev);

	if (k_sem_take(&data->uart_rx_sem, K_MSEC(ID809_UART_TIMEOUT_MS)) != 0) {
		uart_irq_rx_disable(cfg->uart_dev);
		LOG_ERR("UART RX timeout");
		return -ETIMEDOUT;
	}

	if (data->rx_error == ID809_RX_OVERFLOW) {
		LOG_ERR("RX buffer overflow");
		return -EOVERFLOW;
	}

	if (data->rx_error == ID809_RX_INVALID_LEN) {
		LOG_ERR("Invalid packet length");
		return -EBADMSG;
	}

	LOG_HEXDUMP_DBG(data->rx_pkt.buf, data->rx_pkt.len, "RX");

	total_len = data->rx_pkt.len;

	/* Validate prefix */
	uint8_t b0 = data->rx_pkt.buf[0];
	uint8_t b1 = data->rx_pkt.buf[1];
	bool is_rcm = (b0 == ID809_RCM_PREFIX_B0 && b1 == ID809_RCM_PREFIX_B1);
	bool is_dat = (b0 == ID809_DAT_PREFIX_B0 && b1 == ID809_DAT_PREFIX_B1);

	if (!is_rcm && !is_dat) {
		LOG_ERR("Invalid prefix: 0x%02x 0x%02x", b0, b1);
		return -EBADMSG;
	}

	if (total_len < ID809_EXTHDR_SIZE + ID809_CKS_SIZE) {
		LOG_ERR("Packet too short: %u bytes", total_len);
		return -EBADMSG;
	}

	/* Verify CKS: sum of bytes [2..total_len-3] */
	calc_cks = 0;
	for (uint16_t i = ID809_OFF_SID; i < total_len - ID809_CKS_SIZE; i++) {
		calc_cks += data->rx_pkt.buf[i];
	}
	recv_cks = sys_get_le16(&data->rx_pkt.buf[total_len - ID809_CKS_SIZE]);

	if (calc_cks != recv_cks) {
		LOG_ERR("CKS mismatch: recv=0x%04x calc=0x%04x", recv_cks, calc_cks);
		return -EBADMSG;
	}

	return 0;
}

/*
 * Send a command and receive the ACK response.
 * Returns 0 on success, negative errno on error, or positive ID809 return code
 * on a protocol-level error.
 */
static int id809_transceive(const struct device *dev, uint16_t cmd, const uint8_t *payload,
			    uint16_t payload_len)
{
	int ret;

	ret = id809_send_cmd(dev, cmd, payload, payload_len);
	if (ret < 0) {
		return ret;
	}

	ret = id809_recv_pkt(dev);
	if (ret < 0) {
		return ret;
	}

	struct id809_data *data = dev->data;
	uint16_t rc = sys_get_le16(&data->rx_pkt.buf[ID809_OFF_RET]);

	if (rc != ID809_RET_SUCCESS) {
		LOG_DBG("Command 0x%04x returned 0x%04x", cmd, rc);
		return id809_ret_to_errno(rc);
	}

	return 0;
}

/*
 * Send a command, receive the ACK, then receive the DATA packet that follows.
 * The DATA payload (excluding RET) is copied into out_buf (up to out_len bytes).
 * On success, *actual_len is set to the number of data bytes copied.
 */
static int id809_transceive_data(const struct device *dev, uint16_t cmd, const uint8_t *payload,
				 uint16_t payload_len, uint8_t *out_buf, uint16_t out_len,
				 uint16_t *actual_len)
{
	struct id809_data *data = dev->data;
	int ret;

	/* First packet: ACK */
	ret = id809_transceive(dev, cmd, payload, payload_len);
	if (ret < 0) {
		return ret;
	}

	/* Second packet: DATA response */
	ret = id809_recv_pkt(dev);
	if (ret < 0) {
		return ret;
	}

	/* Verify it is a DATA response */
	if (data->rx_pkt.buf[0] != ID809_DAT_PREFIX_B0 ||
	    data->rx_pkt.buf[1] != ID809_DAT_PREFIX_B1) {
		LOG_ERR("Expected DATA response, got 0x%02x 0x%02x", data->rx_pkt.buf[0],
			data->rx_pkt.buf[1]);
		return -EBADMSG;
	}

	uint16_t rc = sys_get_le16(&data->rx_pkt.buf[ID809_OFF_RET]);

	if (rc != ID809_RET_SUCCESS) {
		return id809_ret_to_errno(rc);
	}

	/* DATA payload starts after the extended header */
	uint16_t pkt_len = sys_get_le16(&data->rx_pkt.buf[ID809_OFF_LEN]);

	if (pkt_len < ID809_CKS_SIZE) {
		return -EBADMSG;
	}

	uint16_t dat_len = pkt_len - ID809_CKS_SIZE;

	if (out_buf != NULL && actual_len != NULL) {
		uint16_t copy = MIN(dat_len, out_len);

		memcpy(out_buf, &data->rx_pkt.buf[ID809_OFF_RXPAY], copy);
		*actual_len = copy;
	}

	return 0;
}

/* Poll for finger presence until timeout */
static int id809_poll_finger(const struct device *dev, uint32_t timeout_ms)
{
	struct id809_data *data = dev->data;
	const int64_t start = k_uptime_get();
	int ret;

	do {
		ret = id809_transceive(dev, ID809_CMD_FINGER_DETECT, NULL, 0);
		if (ret < 0) {
			return ret;
		}

		/*
		 * CMD_FINGER_DETECT always returns RET=SUCCESS.  The first byte
		 * of additional payload is 1 when a finger is present, 0 when not.
		 */
		if (data->rx_pkt.buf[ID809_OFF_RXPAY] != 0) {
			return 0;
		}

		k_msleep(ID809_FINGER_POLL_MS);
	} while ((k_uptime_get() - start) < timeout_ms);

	return -ETIMEDOUT;
}

/* Capture one fingerprint sample into a RAM buffer */
static int id809_capture_sample(const struct device *dev, uint8_t ram_buf, uint32_t timeout_ms)
{
	uint8_t param[2];
	int ret;

	ret = id809_poll_finger(dev, timeout_ms);
	if (ret < 0) {
		return ret;
	}

	ret = id809_transceive(dev, ID809_CMD_GET_IMAGE, NULL, 0);
	if (ret < 0) {
		LOG_ERR("GET_IMAGE failed: %d", ret);
		return ret;
	}

	sys_put_le16(ram_buf, param);
	ret = id809_transceive(dev, ID809_CMD_GENERATE, param, 2);
	if (ret < 0) {
		LOG_ERR("GENERATE failed: %d", ret);
		return ret;
	}

	return 0;
}

/* ---------- Biometric API ---------- */

static int id809_get_capabilities(const struct device *dev, struct biometric_capabilities *caps)
{
	struct id809_data *data = dev->data;

	caps->type = BIOMETRIC_TYPE_FINGERPRINT;
	caps->max_templates = data->max_templates;
	caps->template_size = 0; /* Not exposable via this protocol */
	caps->storage_modes = BIOMETRIC_STORAGE_DEVICE;
	caps->enrollment_samples_required = ID809_ENROLL_SAMPLES;

	return 0;
}

static int id809_attr_set(const struct device *dev, enum biometric_attribute attr, int32_t val)
{
	struct id809_data *data = dev->data;
	uint8_t params[5] = {0};
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	switch (attr) {
	case BIOMETRIC_ATTR_SECURITY_LEVEL:
		if (val < 1 || val > 5) {
			ret = -EINVAL;
			break;
		}
		params[0] = ID809_PARAM_SECURITY_LEVEL;
		params[1] = (uint8_t)val;
		ret = id809_transceive(dev, ID809_CMD_SET_PARAM, params, 5);
		if (ret == 0) {
			data->security_level = val;
		} else {
			LOG_ERR("Failed to set security level: %d", ret);
		}
		break;

	case BIOMETRIC_ATTR_TIMEOUT_MS:
		if (val < 0 || (uint32_t)val > ID809_MAX_TIMEOUT_MS) {
			ret = -EINVAL;
		} else {
			data->timeout_ms = (uint32_t)val;
			ret = 0;
		}
		break;

	case BIOMETRIC_ATTR_IMAGE_QUALITY:
		ret = -EACCES;
		break;

	default:
		ret = -ENOTSUP;
		break;
	}

	k_mutex_unlock(&data->lock);
	return ret;
}

static int id809_attr_get(const struct device *dev, enum biometric_attribute attr, int32_t *val)
{
	struct id809_data *data = dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	switch (attr) {
	case BIOMETRIC_ATTR_SECURITY_LEVEL:
		*val = data->security_level;
		break;
	case BIOMETRIC_ATTR_TIMEOUT_MS:
		*val = (int32_t)data->timeout_ms;
		break;
	case BIOMETRIC_ATTR_IMAGE_QUALITY:
		/* ID809 does not report per-image quality scores */
		*val = 0;
		break;
	default:
		ret = -ENOTSUP;
		break;
	}

	k_mutex_unlock(&data->lock);
	return ret;
}

static int id809_enroll_start(const struct device *dev, uint16_t template_id)
{
	struct id809_data *data = dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->enroll_state != ID809_ENROLL_IDLE) {
		ret = -EBUSY;
		goto unlock;
	}

	if (template_id == 0 || template_id > data->max_templates) {
		ret = -EINVAL;
		goto unlock;
	}

	data->enroll_state = ID809_ENROLL_SAMPLE_0;
	data->enroll_id = template_id;

	LOG_INF("Enrollment started for ID %d", template_id);

unlock:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int id809_enroll_capture(const struct device *dev, k_timeout_t timeout,
				struct biometric_capture_result *result)
{
	struct id809_data *data = dev->data;
	enum id809_enroll_state current_state;
	uint32_t timeout_ms;
	uint8_t ram_buf;
	uint8_t samples_done;
	int ret;

	if (K_TIMEOUT_EQ(timeout, K_FOREVER)) {
		timeout_ms = data->timeout_ms;
	} else if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
		timeout_ms = 0;
	} else {
		int64_t ms = k_ticks_to_ms_ceil64(timeout.ticks);

		if (ms > ID809_MAX_TIMEOUT_MS) {
			return -EINVAL;
		}
		timeout_ms = (uint32_t)ms;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	current_state = data->enroll_state;

	if (current_state == ID809_ENROLL_IDLE) {
		k_mutex_unlock(&data->lock);
		return -EINVAL;
	}

	if (current_state == ID809_ENROLL_READY) {
		k_mutex_unlock(&data->lock);
		return -EALREADY;
	}

	switch (current_state) {
	case ID809_ENROLL_SAMPLE_0:
		ram_buf = ID809_RAM_BUF_0;
		samples_done = 1;
		break;
	case ID809_ENROLL_SAMPLE_1:
		ram_buf = ID809_RAM_BUF_1;
		samples_done = 2;
		break;
	case ID809_ENROLL_SAMPLE_2:
	default:
		ram_buf = ID809_RAM_BUF_2;
		samples_done = 3;
		break;
	}

	ret = id809_capture_sample(dev, ram_buf, timeout_ms);
	if (ret < 0) {
		data->enroll_state = ID809_ENROLL_IDLE;
		k_mutex_unlock(&data->lock);
		return ret;
	}

	if (current_state == ID809_ENROLL_SAMPLE_0) {
		data->enroll_state = ID809_ENROLL_SAMPLE_1;
	} else if (current_state == ID809_ENROLL_SAMPLE_1) {
		data->enroll_state = ID809_ENROLL_SAMPLE_2;
	} else {
		data->enroll_state = ID809_ENROLL_READY;
	}

	if (result != NULL) {
		result->samples_captured = samples_done;
		result->samples_required = ID809_ENROLL_SAMPLES;
		result->quality = 0; /* ID809 does not report per-sample quality */
	}

	k_mutex_unlock(&data->lock);

	LOG_INF("Enrollment capture %d/%d completed", samples_done, ID809_ENROLL_SAMPLES);

	return 0;
}

static int id809_enroll_finalize(const struct device *dev)
{
	struct id809_data *data = dev->data;
	uint8_t params[4] = {0};
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->enroll_state != ID809_ENROLL_READY) {
		k_mutex_unlock(&data->lock);
		return -EINVAL;
	}

	/* Merge all three sample buffers into a single template */
	uint8_t merge_params[3] = {0, 0, ID809_ENROLL_SAMPLES};

	ret = id809_transceive(dev, ID809_CMD_MERGE, merge_params, sizeof(merge_params));
	if (ret < 0) {
		data->enroll_state = ID809_ENROLL_IDLE;
		k_mutex_unlock(&data->lock);
		LOG_ERR("Merge failed: %d", ret);
		return ret;
	}

	/* Store the merged template at the requested ID */
	params[0] = (uint8_t)(data->enroll_id & 0xFF);
	params[1] = (uint8_t)(data->enroll_id >> 8);

	ret = id809_transceive(dev, ID809_CMD_STORE_CHAR, params, sizeof(params));
	if (ret < 0) {
		data->enroll_state = ID809_ENROLL_IDLE;
		k_mutex_unlock(&data->lock);
		LOG_ERR("Store failed: %d", ret);
		return ret;
	}

	LOG_INF("Enrollment completed for ID %d", data->enroll_id);
	data->enroll_state = ID809_ENROLL_IDLE;

	k_mutex_unlock(&data->lock);

	return 0;
}

static int id809_enroll_abort(const struct device *dev)
{
	struct id809_data *data = dev->data;
	bool was_idle;

	k_mutex_lock(&data->lock, K_FOREVER);
	was_idle = (data->enroll_state == ID809_ENROLL_IDLE);
	data->enroll_state = ID809_ENROLL_IDLE;
	k_mutex_unlock(&data->lock);

	if (was_idle) {
		return -EALREADY;
	}

	LOG_INF("Enrollment aborted");
	return 0;
}

static int id809_template_delete(const struct device *dev, uint16_t id)
{
	struct id809_data *data = dev->data;
	uint8_t params[4] = {0};
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	/* CMD_DEL_CHAR payload: [start_id_L, start_id_H, end_id_L, end_id_H] */
	params[0] = (uint8_t)(id & 0xFF);
	params[1] = (uint8_t)(id >> 8);
	params[2] = (uint8_t)(id & 0xFF);
	params[3] = (uint8_t)(id >> 8);

	ret = id809_transceive(dev, ID809_CMD_DEL_CHAR, params, sizeof(params));

	k_mutex_unlock(&data->lock);
	return ret;
}

static int id809_template_delete_all(const struct device *dev)
{
	struct id809_data *data = dev->data;
	uint8_t params[4] = {0};
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	/* Delete range: ID 1 to max_templates */
	params[0] = 0x01;
	params[1] = 0x00;
	params[2] = (uint8_t)(data->max_templates & 0xFF);
	params[3] = (uint8_t)(data->max_templates >> 8);

	ret = id809_transceive(dev, ID809_CMD_DEL_CHAR, params, sizeof(params));

	k_mutex_unlock(&data->lock);
	return ret;
}

static int id809_template_list(const struct device *dev, uint16_t *ids, size_t max_count,
				size_t *actual_count)
{
	struct id809_data *data = dev->data;
	/*
	 * The enrolled ID list bitmap has one bit per fingerprint slot.
	 * For a capacity of 80, this is 80 / 8 = 10 bytes. We allocate
	 * a buffer large enough for up to 256 fingerprints (32 bytes).
	 */
	uint8_t bitmap[32];
	uint16_t bitmap_len;
	size_t count = 0;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	ret = id809_transceive_data(dev, ID809_CMD_GET_ENROLLED_ID_LIST, NULL, 0, bitmap,
				    sizeof(bitmap), &bitmap_len);
	if (ret < 0) {
		k_mutex_unlock(&data->lock);
		return ret;
	}

	/* Walk the bitmap: bit N set means template (N+1) is stored */
	for (uint16_t i = 0; i < bitmap_len * 8U && count < max_count; i++) {
		if (bitmap[i / 8] & BIT(i % 8)) {
			ids[count++] = i + 1U;
		}
	}

	*actual_count = count;

	k_mutex_unlock(&data->lock);

	return 0;
}

static int id809_match(const struct device *dev, enum biometric_match_mode mode,
		       uint16_t template_id, k_timeout_t timeout,
		       struct biometric_match_result *result)
{
	struct id809_data *data = dev->data;
	uint32_t timeout_ms;
	int ret;

	if (K_TIMEOUT_EQ(timeout, K_FOREVER)) {
		timeout_ms = data->timeout_ms;
	} else if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
		timeout_ms = 0;
	} else {
		int64_t ms = k_ticks_to_ms_ceil64(timeout.ticks);

		if (ms > ID809_MAX_TIMEOUT_MS) {
			return -EINVAL;
		}
		timeout_ms = (uint32_t)ms;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	/* Capture one sample into RAM buffer 0 */
	ret = id809_capture_sample(dev, ID809_RAM_BUF_0, timeout_ms);
	if (ret < 0) {
		k_mutex_unlock(&data->lock);
		return ret;
	}

	if (mode == BIOMETRIC_MATCH_VERIFY) {
		uint8_t params[4] = {0};

		/*
		 * CMD_VERIFY: compare RAM buffer 0 template against the
		 * stored template at template_id.
		 * Payload: [id_L, id_H, 0, 0]
		 */
		params[0] = (uint8_t)(template_id & 0xFF);
		params[1] = (uint8_t)(template_id >> 8);

		ret = id809_transceive(dev, ID809_CMD_VERIFY, params, sizeof(params));
		if (ret < 0) {
			LOG_DBG("Verify failed: %d", ret);
			k_mutex_unlock(&data->lock);
			return ret;
		}

		if (result != NULL) {
			result->confidence = 100;
			result->template_id = template_id;
			result->image_quality = 0;
		}
		LOG_INF("Verify match: ID %d", template_id);

	} else { /* BIOMETRIC_MATCH_IDENTIFY */
		uint8_t params[6] = {0};
		uint8_t matched_id;

		/*
		 * CMD_SEARCH: 1:N search in the full ID range.
		 * Payload: [rambuf_L, rambuf_H, start_id_L, start_id_H,
		 *           end_id_L, end_id_H]
		 */
		params[0] = ID809_RAM_BUF_0;
		params[2] = 0x01; /* Start ID = 1 */
		params[4] = (uint8_t)(data->max_templates & 0xFF);
		params[5] = (uint8_t)(data->max_templates >> 8);

		ret = id809_transceive(dev, ID809_CMD_SEARCH, params, sizeof(params));
		if (ret < 0) {
			LOG_DBG("Search failed: %d", ret);
			k_mutex_unlock(&data->lock);
			return ret;
		}

		matched_id = data->rx_pkt.buf[ID809_OFF_RXPAY];
		data->last_match_id = matched_id;

		if (result != NULL) {
			result->confidence = 100;
			result->template_id = matched_id;
			result->image_quality = 0;
		}
		LOG_INF("Search match: ID %d", matched_id);
	}

	k_mutex_unlock(&data->lock);

	return 0;
}

static int id809_led_control(const struct device *dev, enum biometric_led_state state)
{
	struct id809_data *data = dev->data;
	uint8_t params[4] = {0};
	int ret;

	/*
	 * CMD_SLED_CTRL payload: [mode, color, color, blink_count]
	 * (color is repeated twice for some firmware versions).
	 */
	switch (state) {
	case BIOMETRIC_LED_OFF:
		params[0] = ID809_LED_OFF;
		params[1] = ID809_LED_COLOR_BLUE;
		params[2] = ID809_LED_COLOR_BLUE;
		break;

	case BIOMETRIC_LED_ON:
		params[0] = ID809_LED_ON;
		params[1] = ID809_LED_COLOR_BLUE;
		params[2] = ID809_LED_COLOR_BLUE;
		break;

	case BIOMETRIC_LED_BLINK:
		params[0] = ID809_LED_FAST_BLINK;
		params[1] = ID809_LED_COLOR_BLUE;
		params[2] = ID809_LED_COLOR_BLUE;
		params[3] = 0; /* Blink indefinitely */
		break;

	case BIOMETRIC_LED_BREATHE:
		params[0] = ID809_LED_BREATHING;
		params[1] = ID809_LED_COLOR_BLUE;
		params[2] = ID809_LED_COLOR_BLUE;
		params[3] = 0; /* Breathe indefinitely */
		break;

	default:
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = id809_transceive(dev, ID809_CMD_SLED_CTRL, params, sizeof(params));

	if (ret == 0) {
		data->led_state = state;
	}

	k_mutex_unlock(&data->lock);
	return ret;
}

static DEVICE_API(biometric, biometrics_id809_api) = {
	.get_capabilities = id809_get_capabilities,
	.attr_set = id809_attr_set,
	.attr_get = id809_attr_get,
	.enroll_start = id809_enroll_start,
	.enroll_capture = id809_enroll_capture,
	.enroll_finalize = id809_enroll_finalize,
	.enroll_abort = id809_enroll_abort,
	.template_store = NULL,
	.template_read = NULL,
	.template_delete = id809_template_delete,
	.template_delete_all = id809_template_delete_all,
	.template_list = id809_template_list,
	.match = id809_match,
	.led_control = id809_led_control,
};

static int id809_init(const struct device *dev)
{
	const struct id809_config *cfg = dev->config;
	struct id809_data *data = dev->data;
	int ret;

	if (!device_is_ready(cfg->uart_dev)) {
		LOG_ERR("UART device not ready");
		return -ENODEV;
	}

	data->dev = dev;
	data->enroll_state = ID809_ENROLL_IDLE;
	data->max_templates = cfg->max_templates;
	data->timeout_ms = CONFIG_ID809_TIMEOUT_MS;
	data->security_level = 3; /* ID809 default security level */
	data->led_state = BIOMETRIC_LED_OFF;
	data->rx_error = ID809_RX_OK;
	data->last_match_id = 0;

	k_mutex_init(&data->lock);
	k_sem_init(&data->uart_tx_sem, 0, 1);
	k_sem_init(&data->uart_rx_sem, 0, 1);

	uart_irq_callback_user_data_set(cfg->uart_dev, id809_uart_callback, data);
	uart_irq_rx_disable(cfg->uart_dev);
	uart_irq_tx_disable(cfg->uart_dev);

	/* Verify the module is reachable */
	ret = id809_transceive(dev, ID809_CMD_TEST_CONNECTION, NULL, 0);
	if (ret < 0) {
		LOG_ERR("Module not responding: %d", ret);
		return ret;
	}

	LOG_INF("ID809 initialized (capacity: %d)", data->max_templates);

	return 0;
}

#define ID809_DEFINE(inst)                                                                         \
	static struct id809_data id809_data_##inst;                                                \
                                                                                                   \
	static const struct id809_config id809_config_##inst = {                                   \
		.uart_dev = DEVICE_DT_GET(DT_INST_BUS(inst)),                                      \
		.max_templates = DT_INST_PROP_OR(inst, max_templates, ID809_DEFAULT_CAPACITY),     \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, id809_init, NULL, &id809_data_##inst,                         \
			      &id809_config_##inst, POST_KERNEL,                                   \
			      CONFIG_BIOMETRICS_INIT_PRIORITY, &biometrics_id809_api);

DT_INST_FOREACH_STATUS_OKAY(ID809_DEFINE)
