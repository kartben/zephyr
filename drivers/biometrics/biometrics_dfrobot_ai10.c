/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Protocol reference: https://github.com/DFRobot/DFRobot_AI10
 */

#define DT_DRV_COMPAT dfrobot_ai10

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/biometrics.h>
#include <zephyr/drivers/biometrics/dfrobot_ai10.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

#include <string.h>

LOG_MODULE_REGISTER(dfrobot_ai10, CONFIG_BIOMETRICS_LOG_LEVEL);

#define AI10_SYNC_H 0xEF
#define AI10_SYNC_L 0xAA

/* Message IDs of module-to-host frames */
#define AI10_MID_RELAY 0x00
#define AI10_MID_NOTE  0x01

#define AI10_CMD_RESET          0x10
#define AI10_CMD_VERIFY         0x12
#define AI10_CMD_ENROLL_SINGLE  0x1D
#define AI10_CMD_DEL_USER       0x20
#define AI10_CMD_DEL_ALL_USER   0x21
#define AI10_CMD_GET_ALL_USERID 0x24
#define AI10_MID_SCAN_QR_CODE   0x70

#define AI10_RESULT_SUCCESS       0
#define AI10_RESULT_REJECTED      1
#define AI10_RESULT_ABORTED       2
#define AI10_RESULT_CAMERA        4
#define AI10_RESULT_INVALID_PARAM 6
#define AI10_RESULT_NO_MEMORY     7
#define AI10_RESULT_UNKNOWN_USER  8
#define AI10_RESULT_MAX_USER      9
#define AI10_RESULT_DUPLICATE     10
#define AI10_RESULT_LIVE_CHECK    12
#define AI10_RESULT_TIMEOUT       13

#define AI10_MAX_USERS DFROBOT_AI10_UID_PALM_MAX

#define AI10_HDR_SIZE      3U /* message ID + 16-bit payload length */
#define AI10_MAX_PAYLOAD   300U
#define AI10_MAX_FRAME     (AI10_HDR_SIZE + AI10_MAX_PAYLOAD + 1U)
#define AI10_USER_NAME_LEN 32U

#define AI10_TX_TIMEOUT_MS   1000
#define AI10_CMD_TIMEOUT_MS  3000
#define AI10_REPLY_MARGIN_MS 500

/* The module accepts capture timeouts of 3 to 20 seconds */
#define AI10_CAPTURE_SECS_MIN 3
#define AI10_CAPTURE_SECS_MAX 20

struct ai10_enroll_request {
	uint8_t admin;
	uint8_t user_name[AI10_USER_NAME_LEN];
	uint8_t face_dir;
	uint8_t timeout;
} __packed;

BUILD_ASSERT(sizeof(struct ai10_enroll_request) == 35U);

struct ai10_reply {
	uint8_t mid;
	uint8_t result;
	const uint8_t *data;
	size_t data_len;
};

enum ai10_rx_state {
	AI10_RX_SYNC_H,
	AI10_RX_SYNC_L,
	AI10_RX_HEADER,
	AI10_RX_BODY,
};

enum ai10_rx_error {
	AI10_RX_OK,
	AI10_RX_INVALID_LEN,
};

enum ai10_enroll_phase {
	AI10_ENROLL_IDLE,
	AI10_ENROLL_WAIT_CAPTURE,
	AI10_ENROLL_DONE,
};

struct dfrobot_ai10_config {
	const struct device *uart_dev;
};

struct dfrobot_ai10_data {
	struct k_mutex lock;
	struct k_spinlock irq_lock;
	struct k_sem uart_tx_sem;
	struct k_sem uart_rx_sem;

	struct {
		uint8_t buf[2U + AI10_HDR_SIZE + sizeof(struct ai10_enroll_request) + 1U];
		uint16_t len;
		uint16_t offset;
	} tx;

	struct {
		uint8_t buf[AI10_MAX_FRAME];
		enum ai10_rx_state state;
		enum ai10_rx_error error;
		uint16_t len;
		uint16_t expected;
	} rx;

	int32_t timeout_ms;
	enum ai10_enroll_phase enroll_phase;
	uint16_t enroll_id;
	uint16_t last_enroll_uid;
	uint16_t last_match_uid;
};

static int ai10_result_to_errno(uint8_t result)
{
	switch (result) {
	case AI10_RESULT_SUCCESS:
		return 0;
	case AI10_RESULT_REJECTED:
	case AI10_RESULT_LIVE_CHECK:
		return -EACCES;
	case AI10_RESULT_ABORTED:
		return -ECANCELED;
	case AI10_RESULT_INVALID_PARAM:
		return -EINVAL;
	case AI10_RESULT_NO_MEMORY:
		return -ENOMEM;
	case AI10_RESULT_UNKNOWN_USER:
		return -ENOENT;
	case AI10_RESULT_MAX_USER:
		return -ENOSPC;
	case AI10_RESULT_DUPLICATE:
		return -EEXIST;
	case AI10_RESULT_TIMEOUT:
		return -ETIMEDOUT;
	case AI10_RESULT_CAMERA:
	default:
		return -EIO;
	}
}

static enum biometric_sensor_type ai10_uid_modality(uint16_t uid)
{
	return uid >= DFROBOT_AI10_UID_PALM_MIN ? BIOMETRIC_TYPE_PALM : BIOMETRIC_TYPE_FACE;
}

static const char *ai10_modality_name(enum biometric_sensor_type modality)
{
	return modality == BIOMETRIC_TYPE_PALM ? "palm" : "face";
}

/* UART IRQ handlers */
static void ai10_uart_tx_handler(const struct device *uart_dev, struct dfrobot_ai10_data *data)
{
	int sent;
	k_spinlock_key_t key = k_spin_lock(&data->irq_lock);

	uint16_t remaining = data->tx.len - data->tx.offset;

	if (remaining > 0) {
		sent = uart_fifo_fill(uart_dev, &data->tx.buf[data->tx.offset], remaining);
		if (sent > 0) {
			data->tx.offset += sent;
			remaining = data->tx.len - data->tx.offset;
		}
	}

	if (remaining == 0) {
		uart_irq_tx_disable(uart_dev);
		k_spin_unlock(&data->irq_lock, key);
		k_sem_give(&data->uart_tx_sem);
		return;
	}

	k_spin_unlock(&data->irq_lock, key);
}

static void ai10_uart_rx_handler(const struct device *uart_dev, struct dfrobot_ai10_data *data)
{
	uint8_t byte;
	k_spinlock_key_t key;

	while (uart_fifo_read(uart_dev, &byte, 1) > 0) {
		key = k_spin_lock(&data->irq_lock);

		switch (data->rx.state) {
		case AI10_RX_SYNC_H:
			if (byte == AI10_SYNC_H) {
				data->rx.state = AI10_RX_SYNC_L;
			}
			break;
		case AI10_RX_SYNC_L:
			if (byte == AI10_SYNC_L) {
				data->rx.state = AI10_RX_HEADER;
				data->rx.len = 0;
			} else if (byte != AI10_SYNC_H) {
				data->rx.state = AI10_RX_SYNC_H;
			}
			break;
		case AI10_RX_HEADER:
			data->rx.buf[data->rx.len++] = byte;
			if (data->rx.len == AI10_HDR_SIZE) {
				uint16_t payload_len = sys_get_be16(&data->rx.buf[1]);

				if (payload_len > AI10_MAX_PAYLOAD) {
					data->rx.error = AI10_RX_INVALID_LEN;
					uart_irq_rx_disable(uart_dev);
					k_spin_unlock(&data->irq_lock, key);
					k_sem_give(&data->uart_rx_sem);
					return;
				}
				data->rx.expected = AI10_HDR_SIZE + payload_len + 1U;
				data->rx.state = AI10_RX_BODY;
			}
			break;
		case AI10_RX_BODY:
			data->rx.buf[data->rx.len++] = byte;
			if (data->rx.len == data->rx.expected) {
				data->rx.state = AI10_RX_SYNC_H;
				uart_irq_rx_disable(uart_dev);
				k_spin_unlock(&data->irq_lock, key);
				k_sem_give(&data->uart_rx_sem);
				return;
			}
			break;
		}

		k_spin_unlock(&data->irq_lock, key);
	}
}

static void ai10_uart_callback(const struct device *uart_dev, void *user_data)
{
	struct dfrobot_ai10_data *data = user_data;

	uart_irq_update(uart_dev);

	if (uart_irq_tx_ready(uart_dev)) {
		ai10_uart_tx_handler(uart_dev, data);
	}

	if (uart_irq_rx_ready(uart_dev)) {
		ai10_uart_rx_handler(uart_dev, data);
	}
}

static int ai10_send_cmd(const struct device *dev, uint8_t cmd, const uint8_t *payload,
			 uint16_t payload_len)
{
	const struct dfrobot_ai10_config *cfg = dev->config;
	struct dfrobot_ai10_data *data = dev->data;
	uint16_t total_len = 2U + AI10_HDR_SIZE + payload_len + 1U;
	uint8_t checksum = 0;
	k_spinlock_key_t key;

	if (total_len > sizeof(data->tx.buf)) {
		return -ENOMEM;
	}

	data->tx.buf[0] = AI10_SYNC_H;
	data->tx.buf[1] = AI10_SYNC_L;
	data->tx.buf[2] = cmd;
	sys_put_be16(payload_len, &data->tx.buf[3]);

	if (payload_len > 0) {
		memcpy(&data->tx.buf[5], payload, payload_len);
	}

	/* XOR of every byte after the sync word */
	for (uint16_t i = 2; i < total_len - 1U; i++) {
		checksum ^= data->tx.buf[i];
	}
	data->tx.buf[total_len - 1U] = checksum;

	key = k_spin_lock(&data->irq_lock);
	data->tx.len = total_len;
	data->tx.offset = 0;
	k_spin_unlock(&data->irq_lock, key);

	LOG_HEXDUMP_DBG(data->tx.buf, total_len, "TX");

	k_sem_reset(&data->uart_tx_sem);
	uart_irq_tx_enable(cfg->uart_dev);

	if (k_sem_take(&data->uart_tx_sem, K_MSEC(AI10_TX_TIMEOUT_MS)) != 0) {
		uart_irq_tx_disable(cfg->uart_dev);
		LOG_ERR("UART TX timeout");
		return -ETIMEDOUT;
	}

	return 0;
}

static int ai10_recv_frame(const struct device *dev, int64_t deadline)
{
	const struct dfrobot_ai10_config *cfg = dev->config;
	struct dfrobot_ai10_data *data = dev->data;
	int64_t remaining = deadline - k_uptime_get();
	uint8_t checksum = 0;
	k_spinlock_key_t key;

	if (remaining <= 0) {
		return -ETIMEDOUT;
	}

	key = k_spin_lock(&data->irq_lock);
	data->rx.state = AI10_RX_SYNC_H;
	data->rx.error = AI10_RX_OK;
	data->rx.len = 0;
	data->rx.expected = 0;
	k_spin_unlock(&data->irq_lock, key);

	k_sem_reset(&data->uart_rx_sem);
	uart_irq_rx_enable(cfg->uart_dev);

	if (k_sem_take(&data->uart_rx_sem, K_MSEC(remaining)) != 0) {
		uart_irq_rx_disable(cfg->uart_dev);
		return -ETIMEDOUT;
	}

	if (data->rx.error != AI10_RX_OK) {
		LOG_ERR("Invalid frame length");
		return -EBADMSG;
	}

	LOG_HEXDUMP_DBG(data->rx.buf, data->rx.len, "RX");

	/* XOR of message ID, length and payload */
	for (uint16_t i = 0; i < data->rx.len - 1U; i++) {
		checksum ^= data->rx.buf[i];
	}

	if (checksum != data->rx.buf[data->rx.len - 1U]) {
		LOG_ERR("Checksum mismatch");
		return -EBADMSG;
	}

	return 0;
}

static int ai10_recv_reply(const struct device *dev, uint8_t cmd, int64_t deadline,
			   struct ai10_reply *reply)
{
	struct dfrobot_ai10_data *data = dev->data;

	while (true) {
		uint16_t payload_len;
		uint8_t msg_id;
		int ret = ai10_recv_frame(dev, deadline);

		if (ret == -ETIMEDOUT) {
			LOG_ERR("Timed out waiting for reply to command 0x%02x", cmd);
			return ret;
		}
		if (ret < 0) {
			/* Drop the bad frame and resynchronize on the next one */
			continue;
		}

		msg_id = data->rx.buf[0];
		payload_len = data->rx.len - AI10_HDR_SIZE - 1U;

		/* NOTE frames report detection progress, only the RELAY frame ends a command */
		if (msg_id == AI10_MID_NOTE) {
			continue;
		}

		if (msg_id != AI10_MID_RELAY || payload_len < 2U) {
			LOG_WRN("Unexpected frame (message ID 0x%02x, length %u)", msg_id,
				payload_len);
			continue;
		}

		reply->mid = data->rx.buf[AI10_HDR_SIZE];
		reply->result = data->rx.buf[AI10_HDR_SIZE + 1U];
		reply->data = &data->rx.buf[AI10_HDR_SIZE + 2U];
		reply->data_len = payload_len - 2U;
		return 0;
	}
}

static int ai10_transceive(const struct device *dev, uint8_t cmd, const uint8_t *payload,
			   uint16_t payload_len, uint32_t reply_ms, struct ai10_reply *reply)
{
	int64_t deadline = k_uptime_get() + reply_ms + AI10_REPLY_MARGIN_MS;
	int ret;

	ret = ai10_send_cmd(dev, cmd, payload, payload_len);
	if (ret < 0) {
		return ret;
	}

	return ai10_recv_reply(dev, cmd, deadline, reply);
}

static int ai10_command(const struct device *dev, uint8_t cmd, const uint8_t *payload,
			uint16_t payload_len, uint32_t reply_ms, struct ai10_reply *reply)
{
	struct ai10_reply local;
	int ret;

	if (reply == NULL) {
		reply = &local;
	}

	ret = ai10_transceive(dev, cmd, payload, payload_len, reply_ms, reply);
	if (ret < 0) {
		return ret;
	}

	if (reply->mid != cmd) {
		LOG_ERR("Reply to command 0x%02x has unexpected ID 0x%02x", cmd, reply->mid);
		return -EBADMSG;
	}

	ret = ai10_result_to_errno(reply->result);
	if (ret < 0) {
		LOG_ERR("Command 0x%02x failed with module result %u", cmd, reply->result);
	}

	return ret;
}

/*
 * The vendor library issues a RESET before every management command to abort
 * any capture in progress and resynchronize with the module.
 */
static int ai10_sync(const struct device *dev)
{
	return ai10_command(dev, AI10_CMD_RESET, NULL, 0, AI10_CMD_TIMEOUT_MS, NULL);
}

static uint8_t ai10_capture_secs(struct dfrobot_ai10_data *data, k_timeout_t timeout)
{
	int64_t ms;

	if (K_TIMEOUT_EQ(timeout, K_FOREVER)) {
		ms = data->timeout_ms;
	} else {
		ms = k_ticks_to_ms_ceil64(timeout.ticks);
	}

	return (uint8_t)CLAMP(DIV_ROUND_UP(ms, MSEC_PER_SEC), AI10_CAPTURE_SECS_MIN,
			      AI10_CAPTURE_SECS_MAX);
}

/* Biometric API implementations */

static int dfrobot_ai10_get_capabilities(const struct device *dev,
					 struct biometric_capabilities *caps)
{
	ARG_UNUSED(dev);

	caps->supported_modalities = BIT(BIOMETRIC_TYPE_FACE) | BIT(BIOMETRIC_TYPE_PALM);
	caps->max_templates = AI10_MAX_USERS;
	caps->template_size = 0;
	caps->storage_modes = BIOMETRIC_STORAGE_DEVICE;
	caps->enrollment_samples_required = 1;

	return 0;
}

static int dfrobot_ai10_attr_set(const struct device *dev, enum biometric_attribute attr,
				 int32_t val)
{
	struct dfrobot_ai10_data *data = dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	switch ((int)attr) {
	case BIOMETRIC_ATTR_TIMEOUT_MS:
		if (val < AI10_CAPTURE_SECS_MIN * MSEC_PER_SEC ||
		    val > AI10_CAPTURE_SECS_MAX * MSEC_PER_SEC) {
			ret = -EINVAL;
		} else {
			data->timeout_ms = val;
		}
		break;
	case BIOMETRIC_ATTR_IMAGE_QUALITY:
	case DFROBOT_AI10_ATTR_LAST_MATCH_UID:
	case DFROBOT_AI10_ATTR_LAST_ENROLL_UID:
		ret = -EACCES;
		break;
	default:
		ret = -ENOTSUP;
		break;
	}

	k_mutex_unlock(&data->lock);
	return ret;
}

static int dfrobot_ai10_attr_get(const struct device *dev, enum biometric_attribute attr,
				 int32_t *val)
{
	struct dfrobot_ai10_data *data = dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	switch ((int)attr) {
	case BIOMETRIC_ATTR_TIMEOUT_MS:
		*val = data->timeout_ms;
		break;
	case DFROBOT_AI10_ATTR_LAST_MATCH_UID:
		*val = data->last_match_uid;
		break;
	case DFROBOT_AI10_ATTR_LAST_ENROLL_UID:
		*val = data->last_enroll_uid;
		break;
	default:
		ret = -ENOTSUP;
		break;
	}

	k_mutex_unlock(&data->lock);
	return ret;
}

static int dfrobot_ai10_enroll_start(const struct device *dev, uint16_t template_id)
{
	struct dfrobot_ai10_data *data = dev->data;
	int ret = 0;

	if (template_id < 1U || template_id > AI10_MAX_USERS) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->enroll_phase != AI10_ENROLL_IDLE) {
		ret = -EBUSY;
	} else {
		data->enroll_phase = AI10_ENROLL_WAIT_CAPTURE;
		data->enroll_id = template_id;
	}

	k_mutex_unlock(&data->lock);

	if (ret == 0) {
		LOG_INF("Enrollment started for ID %u", template_id);
	}

	return ret;
}

static int dfrobot_ai10_enroll_capture(const struct device *dev, k_timeout_t timeout,
				       struct biometric_capture_result *result)
{
	struct dfrobot_ai10_data *data = dev->data;
	struct ai10_enroll_request req = {0};
	struct ai10_reply reply;
	uint8_t secs;
	uint16_t uid;
	int ret;

	if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->enroll_phase == AI10_ENROLL_DONE) {
		ret = -EALREADY;
		goto out;
	}
	if (data->enroll_phase != AI10_ENROLL_WAIT_CAPTURE) {
		ret = -EINVAL;
		goto out;
	}

	secs = ai10_capture_secs(data, timeout);
	req.timeout = secs;
	snprintk((char *)req.user_name, sizeof(req.user_name), "user%u", data->enroll_id);

	ret = ai10_sync(dev);
	if (ret < 0) {
		goto out;
	}

	ret = ai10_command(dev, AI10_CMD_ENROLL_SINGLE, (const uint8_t *)&req, sizeof(req),
			   secs * MSEC_PER_SEC, &reply);
	if (ret < 0) {
		goto out;
	}

	if (reply.data_len < 2U) {
		ret = -EBADMSG;
		goto out;
	}

	uid = sys_get_be16(reply.data);
	data->last_enroll_uid = uid;
	data->enroll_phase = AI10_ENROLL_DONE;

	LOG_INF("Enrolled %s as user ID %u", ai10_modality_name(ai10_uid_modality(uid)), uid);
	if (uid != data->enroll_id) {
		LOG_WRN("Module assigned ID %u instead of requested ID %u", uid, data->enroll_id);
	}

	if (result != NULL) {
		result->modality = ai10_uid_modality(uid);
		result->samples_captured = 1;
		result->samples_required = 1;
		result->quality = 0;
	}

out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int dfrobot_ai10_enroll_finalize(const struct device *dev)
{
	struct dfrobot_ai10_data *data = dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->enroll_phase != AI10_ENROLL_DONE) {
		ret = -EINVAL;
	} else {
		data->enroll_phase = AI10_ENROLL_IDLE;
		LOG_INF("Enrollment completed for user ID %u", data->last_enroll_uid);
	}

	k_mutex_unlock(&data->lock);
	return ret;
}

static int dfrobot_ai10_enroll_abort(const struct device *dev)
{
	struct dfrobot_ai10_data *data = dev->data;
	bool was_idle;

	k_mutex_lock(&data->lock, K_FOREVER);
	was_idle = (data->enroll_phase == AI10_ENROLL_IDLE);
	data->enroll_phase = AI10_ENROLL_IDLE;
	k_mutex_unlock(&data->lock);

	if (was_idle) {
		return -EALREADY;
	}

	LOG_INF("Enrollment aborted");
	return 0;
}

static int dfrobot_ai10_template_delete(const struct device *dev, uint16_t id)
{
	struct dfrobot_ai10_data *data = dev->data;
	uint8_t payload[2];
	int ret;

	if (id < 1U || id > AI10_MAX_USERS) {
		return -EINVAL;
	}

	sys_put_be16(id, payload);

	k_mutex_lock(&data->lock, K_FOREVER);

	ret = ai10_sync(dev);
	if (ret == 0) {
		ret = ai10_command(dev, AI10_CMD_DEL_USER, payload, sizeof(payload),
				   AI10_CMD_TIMEOUT_MS, NULL);
	}

	k_mutex_unlock(&data->lock);

	if (ret == 0) {
		LOG_INF("Deleted user ID %u", id);
	}

	return ret;
}

static int dfrobot_ai10_template_delete_all(const struct device *dev)
{
	struct dfrobot_ai10_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	ret = ai10_sync(dev);
	if (ret == 0) {
		ret = ai10_command(dev, AI10_CMD_DEL_ALL_USER, NULL, 0, AI10_CMD_TIMEOUT_MS, NULL);
	}

	k_mutex_unlock(&data->lock);

	if (ret == 0) {
		LOG_INF("Deleted all users");
	}

	return ret;
}

static int dfrobot_ai10_template_list(const struct device *dev, uint16_t *ids, size_t max_count,
				      size_t *actual_count)
{
	struct dfrobot_ai10_data *data = dev->data;
	const uint8_t payload = 0;
	struct ai10_reply reply;
	size_t user_num;
	size_t count;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	ret = ai10_sync(dev);
	if (ret < 0) {
		goto out;
	}

	ret = ai10_command(dev, AI10_CMD_GET_ALL_USERID, &payload, sizeof(payload),
			   AI10_CMD_TIMEOUT_MS, &reply);
	if (ret < 0) {
		goto out;
	}

	if (reply.data_len < 1U) {
		ret = -EBADMSG;
		goto out;
	}

	user_num = reply.data[0];
	count = MIN(MIN(user_num, (reply.data_len - 1U) / 2U), max_count);

	for (size_t i = 0; i < count; i++) {
		ids[i] = sys_get_be16(&reply.data[1U + i * 2U]);
	}

	*actual_count = count;

	if (count < user_num) {
		LOG_WRN("Listed %zu of %zu enrolled users", count, user_num);
	}

out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int dfrobot_ai10_match(const struct device *dev, enum biometric_match_mode mode,
			      uint16_t template_id, k_timeout_t timeout,
			      struct biometric_match_result *result)
{
	struct dfrobot_ai10_data *data = dev->data;
	struct ai10_reply reply;
	uint8_t payload[2];
	uint8_t secs;
	uint16_t uid;
	int ret;

	if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
		return -EINVAL;
	}

	if (mode == BIOMETRIC_MATCH_VERIFY && (template_id < 1U || template_id > AI10_MAX_USERS)) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	secs = ai10_capture_secs(data, timeout);
	payload[0] = 0; /* single-shot recognition, not continuous mode */
	payload[1] = secs;

	ret = ai10_transceive(dev, AI10_CMD_VERIFY, payload, sizeof(payload), secs * MSEC_PER_SEC,
			      &reply);
	if (ret < 0) {
		goto out;
	}

	/* A QR code seen during a verify session is relayed with the QR message ID */
	if (reply.mid == AI10_MID_SCAN_QR_CODE) {
		LOG_WRN("QR code scanned instead of biometric sample");
		ret = -ENOTSUP;
		goto out;
	}

	if (reply.mid != AI10_CMD_VERIFY) {
		LOG_ERR("Reply to VERIFY has unexpected ID 0x%02x", reply.mid);
		ret = -EBADMSG;
		goto out;
	}

	ret = ai10_result_to_errno(reply.result);
	if (ret < 0) {
		LOG_DBG("Match failed with module result %u", reply.result);
		goto out;
	}

	if (reply.data_len < 2U) {
		ret = -EBADMSG;
		goto out;
	}

	uid = sys_get_be16(reply.data);
	data->last_match_uid = uid;

	LOG_INF("Matched %s user ID %u", ai10_modality_name(ai10_uid_modality(uid)), uid);

	if (mode == BIOMETRIC_MATCH_VERIFY && uid != template_id) {
		ret = -ENOENT;
		goto out;
	}

	if (result != NULL) {
		result->confidence = 0;
		result->modality = ai10_uid_modality(uid);
		result->template_id = uid;
		result->image_quality = 0;
	}

out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static DEVICE_API(biometric, dfrobot_ai10_api) = {
	.get_capabilities = dfrobot_ai10_get_capabilities,
	.attr_set = dfrobot_ai10_attr_set,
	.attr_get = dfrobot_ai10_attr_get,
	.enroll_start = dfrobot_ai10_enroll_start,
	.enroll_capture = dfrobot_ai10_enroll_capture,
	.enroll_finalize = dfrobot_ai10_enroll_finalize,
	.enroll_abort = dfrobot_ai10_enroll_abort,
	.template_delete = dfrobot_ai10_template_delete,
	.template_delete_all = dfrobot_ai10_template_delete_all,
	.template_list = dfrobot_ai10_template_list,
	.match = dfrobot_ai10_match,
};

static int dfrobot_ai10_init(const struct device *dev)
{
	const struct dfrobot_ai10_config *cfg = dev->config;
	struct dfrobot_ai10_data *data = dev->data;
	int ret;

	if (!device_is_ready(cfg->uart_dev)) {
		LOG_ERR("UART device not ready");
		return -ENODEV;
	}

	k_mutex_init(&data->lock);
	k_sem_init(&data->uart_tx_sem, 0, 1);
	k_sem_init(&data->uart_rx_sem, 0, 1);

	data->timeout_ms = CONFIG_DFROBOT_AI10_TIMEOUT_MS;

	uart_irq_callback_user_data_set(cfg->uart_dev, ai10_uart_callback, data);
	uart_irq_rx_disable(cfg->uart_dev);
	uart_irq_tx_disable(cfg->uart_dev);

	ret = ai10_sync(dev);
	if (ret < 0) {
		LOG_ERR("Module reset failed: %d", ret);
		return ret;
	}

	LOG_INF("DFRobot AI10 initialized");
	return 0;
}

#define DFROBOT_AI10_DEFINE(inst)                                                                  \
	static struct dfrobot_ai10_data dfrobot_ai10_data_##inst;                                  \
                                                                                                   \
	static const struct dfrobot_ai10_config dfrobot_ai10_config_##inst = {                     \
		.uart_dev = DEVICE_DT_GET(DT_INST_BUS(inst)),                                      \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, dfrobot_ai10_init, NULL, &dfrobot_ai10_data_##inst,            \
			      &dfrobot_ai10_config_##inst, POST_KERNEL,                            \
			      CONFIG_BIOMETRICS_INIT_PRIORITY, &dfrobot_ai10_api);

DT_INST_FOREACH_STATUS_OKAY(DFROBOT_AI10_DEFINE)
