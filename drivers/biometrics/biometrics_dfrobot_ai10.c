/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * UART protocol reference: https://github.com/DFRobot/DFRobot_AI10/tree/master/src
 */

#define DT_DRV_COMPAT dfrobot_ai10

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/biometrics.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(biometrics_dfrobot_ai10, CONFIG_BIOMETRICS_LOG_LEVEL);

#define DFROBOT_AI10_SYNC_H         0xEFU
#define DFROBOT_AI10_SYNC_L         0xAAU
#define DFROBOT_AI10_MID_RELAY      0x00U
#define DFROBOT_AI10_MID_NOTE       0x01U
#define DFROBOT_AI10_RESET          0x10U
#define DFROBOT_AI10_ENROLL_SINGLE  0x1DU
#define DFROBOT_AI10_GET_ALL_USERID 0x24U
#define DFROBOT_AI10_GET_USER_INFO  0x22U
#define DFROBOT_AI10_DEL_USER       0x20U
#define DFROBOT_AI10_DEL_ALL_USER   0x21U
#define DFROBOT_AI10_VERIFY         0x12U
#define DFROBOT_AI10_MID_SCAN_QR    0x70U

#define DFROBOT_AI10_UID_PALM_MIN   1001U
#define DFROBOT_AI10_FRAME_MAX      512U
#define DFROBOT_AI10_RESET_TIMEOUT_MS 3000U

struct dfrobot_ai10_reply {
	uint8_t mid;
	uint8_t result;
	uint8_t data[256];
};

struct dfrobot_ai10_note {
	uint8_t raw[256];
};

struct dfrobot_enroll_payload {
	uint8_t admin;
	uint8_t user_name[32];
	uint8_t face_dir;
	uint8_t timeout;
} __packed;

BUILD_ASSERT(sizeof(struct dfrobot_enroll_payload) == 35U);

struct dfrobot_ai10_config {
	const struct device *uart;
	uint16_t max_users;
};

enum dfrobot_enroll_phase {
	DFROBOT_ENROLL_IDLE = 0,
	DFROBOT_ENROLL_WAIT_CAPTURE,
	DFROBOT_ENROLL_DONE,
};

struct dfrobot_ai10_data {
	struct k_mutex lock;
	const struct device *uart;
	uint16_t max_users;
	int32_t timeout_ms;
	enum dfrobot_enroll_phase enroll_phase;
	uint16_t enroll_template_id;
	uint16_t last_match_uid;
	int32_t last_match_confidence;
	enum biometric_sensor_type last_match_modality;
};

static uint8_t dfrobot_ai10_xor_body(const uint8_t *data, size_t len)
{
	uint8_t x = 0;

	for (size_t i = 0; i < len; i++) {
		x ^= data[i];
	}
	return x;
}

static bool dfrobot_ai10_xor_frame_ok(const uint8_t *frame, size_t len)
{
	if (len < 2U) {
		return false;
	}

	uint8_t x = 0;

	for (size_t i = 0; i < len - 1U; i++) {
		x ^= frame[i];
	}
	return x == frame[len - 1];
}

static void dfrobot_ai10_uart_drain(const struct device *uart)
{
	uint8_t c;

	while (uart_poll_in(uart, &c) == 0) {
		continue;
	}
}

static void dfrobot_ai10_send_packet(const struct device *uart, const uint8_t *body, size_t body_len)
{
	uint8_t par = dfrobot_ai10_xor_body(body, body_len);

	LOG_HEXDUMP_DBG(body, body_len, "dfrobot_ai10 TX payload");
	LOG_DBG("dfrobot_ai10 TX sync + %zu bytes + XOR 0x%02x", body_len, par);

	uart_poll_out(uart, DFROBOT_AI10_SYNC_H);
	uart_poll_out(uart, DFROBOT_AI10_SYNC_L);
	for (size_t i = 0; i < body_len; i++) {
		uart_poll_out(uart, body[i]);
	}
	uart_poll_out(uart, par);
}

static int dfrobot_ai10_read_byte_deadline(const struct device *uart, uint8_t *b, uint64_t deadline)
{
	while (k_uptime_get() < deadline) {
		if (uart_poll_in(uart, b) == 0) {
			return 0;
		}
		k_msleep(1);
	}
	return -ETIMEDOUT;
}

static int dfrobot_ai10_recv_frame(const struct device *uart, uint8_t *buf, size_t cap, size_t *out_len,
				   uint64_t deadline)
{
	while (k_uptime_get() < deadline) {
		uint8_t b;
		int ret = dfrobot_ai10_read_byte_deadline(uart, &b, deadline);

		if (ret != 0) {
			return -ETIMEDOUT;
		}
		if (b != DFROBOT_AI10_SYNC_H) {
			continue;
		}
		ret = dfrobot_ai10_read_byte_deadline(uart, &b, deadline);
		if (ret != 0) {
			continue;
		}
		if (b != DFROBOT_AI10_SYNC_L) {
			continue;
		}
		goto got_sync;
	}
	return -ETIMEDOUT;

got_sync:
	for (int i = 0; i < 3; i++) {
		int ret = dfrobot_ai10_read_byte_deadline(uart, &buf[i], deadline);

		if (ret != 0) {
			return ret;
		}
	}

	uint16_t plen = (uint16_t)((uint16_t)buf[1] << 8) | buf[2];

	if (plen > 300U || (size_t)plen + 4U > cap) {
		LOG_DBG("dfrobot_ai10 RX invalid payload length: %u", plen);
		return -EMSGSIZE;
	}

	for (size_t i = 0; i < (size_t)plen + 1U; i++) {
		int ret = dfrobot_ai10_read_byte_deadline(uart, &buf[3 + i], deadline);

		if (ret != 0) {
			return ret;
		}
	}

	*out_len = 3U + (size_t)plen + 1U;

	if (!dfrobot_ai10_xor_frame_ok(buf, *out_len)) {
		LOG_HEXDUMP_DBG(buf, *out_len, "dfrobot_ai10 RX XOR mismatch");
		return -EBADMSG;
	}
	return 0;
}

static int dfrobot_ai10_recv_until_relay(struct dfrobot_ai10_data *data, uint32_t timeout_ms,
					 struct dfrobot_ai10_reply *reply, struct dfrobot_ai10_note *note)
{
	uint8_t buf[DFROBOT_AI10_FRAME_MAX];
	uint64_t deadline = k_uptime_get() + (uint64_t)timeout_ms;

	memset(note->raw, 0, sizeof(note->raw));

	while (k_uptime_get() < deadline) {
		size_t framelen = 0;
		int ret = dfrobot_ai10_recv_frame(data->uart, buf, sizeof(buf), &framelen, deadline);

		if (ret == -ETIMEDOUT) {
			LOG_ERR("dfrobot_ai10: timed out waiting for relay response");
			return -ETIMEDOUT;
		}
		if (ret < 0) {
			LOG_DBG("dfrobot_ai10: recv_frame dropped: %d", ret);
			continue;
		}
		if (framelen < 4U) {
			LOG_DBG("dfrobot_ai10: short frame len=%zu", framelen);
			continue;
		}

		uint8_t msg_id = buf[0];
		size_t paylen = framelen - 4U;

		if (msg_id == DFROBOT_AI10_MID_NOTE) {
			if (paylen > sizeof(note->raw)) {
				paylen = sizeof(note->raw);
			}
			memcpy(note->raw, &buf[3], paylen);
			LOG_HEXDUMP_DBG(note->raw, paylen, "dfrobot_ai10 NOTE");
			continue;
		}
		if (msg_id == DFROBOT_AI10_MID_RELAY) {
			if (paylen > sizeof(*reply)) {
				paylen = sizeof(*reply);
			}
			memcpy(reply, &buf[3], paylen);
			LOG_DBG("dfrobot_ai10 RELAY mid=0x%02x result=%u paylen=%zu", reply->mid,
				reply->result, paylen);
			return 0;
		}

		LOG_WRN("dfrobot_ai10: unexpected msg_id 0x%02x", msg_id);
	}

	LOG_ERR("dfrobot_ai10: timed out waiting for relay response");
	return -ETIMEDOUT;
}

static int dfrobot_ai10_result_to_errno(uint8_t result)
{
	switch (result) {
	case 0:
		return 0;
	case 8:
		return -ENOENT;
	case 9:
	case 7:
		return -ENOSPC;
	case 10:
		return -EEXIST;
	case 13:
		return -ETIMEDOUT;
	default:
		return -EIO;
	}
}

static enum biometric_sensor_type dfrobot_uid_modality(uint16_t uid)
{
	return uid >= DFROBOT_AI10_UID_PALM_MIN ? BIOMETRIC_TYPE_PALM : BIOMETRIC_TYPE_FACE;
}

static int dfrobot_ai10_do_reset(struct dfrobot_ai10_data *data)
{
	uint8_t body[] = {DFROBOT_AI10_RESET, 0x00, 0x00};
	struct dfrobot_ai10_reply reply;
	struct dfrobot_ai10_note note;

	LOG_DBG("dfrobot_ai10: sending RESET");

	dfrobot_ai10_uart_drain(data->uart);
	dfrobot_ai10_send_packet(data->uart, body, ARRAY_SIZE(body));

	int ret = dfrobot_ai10_recv_until_relay(data, DFROBOT_AI10_RESET_TIMEOUT_MS, &reply, &note);

	if (ret != 0) {
		LOG_ERR("dfrobot_ai10: RESET failed: %d", ret);
		return ret;
	}
	if (reply.mid != DFROBOT_AI10_RESET) {
		LOG_ERR("dfrobot_ai10: RESET unexpected mid=0x%02x (expected 0x%02x)", reply.mid,
			DFROBOT_AI10_RESET);
		return -EIO;
	}

	ret = dfrobot_ai10_result_to_errno(reply.result);
	if (ret != 0) {
		LOG_ERR("dfrobot_ai10: RESET module result=%u", reply.result);
	} else {
		LOG_DBG("dfrobot_ai10: RESET ok");
	}
	return ret;
}

static int dfrobot_ai10_get_capabilities(const struct device *dev, struct biometric_capabilities *caps)
{
	const struct dfrobot_ai10_config *cfg = dev->config;

	caps->type = BIOMETRIC_TYPE_FACE;
	caps->supported_modalities = BIT(BIOMETRIC_TYPE_FACE) | BIT(BIOMETRIC_TYPE_PALM);
	caps->max_templates = cfg->max_users;
	caps->template_size = 0U;
	caps->storage_modes = BIOMETRIC_STORAGE_DEVICE;
	caps->enrollment_samples_required = 1U;

	return 0;
}

static int dfrobot_ai10_attr_set(const struct device *dev, enum biometric_attribute attr, int32_t val)
{
	struct dfrobot_ai10_data *data = dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	switch (attr) {
	case BIOMETRIC_ATTR_TIMEOUT_MS:
		if (val < 0 || val > INT32_MAX) {
			ret = -EINVAL;
		} else {
			LOG_DBG("%s: default timeout_ms=%d", dev->name, (int)val);
			data->timeout_ms = val;
		}
		break;
	case BIOMETRIC_ATTR_MATCH_THRESHOLD:
	case BIOMETRIC_ATTR_ENROLLMENT_QUALITY:
	case BIOMETRIC_ATTR_SECURITY_LEVEL:
	case BIOMETRIC_ATTR_ANTI_SPOOF_LEVEL:
		ret = -ENOTSUP;
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

static int dfrobot_ai10_attr_get(const struct device *dev, enum biometric_attribute attr, int32_t *val)
{
	struct dfrobot_ai10_data *data = dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	switch (attr) {
	case BIOMETRIC_ATTR_TIMEOUT_MS:
		*val = data->timeout_ms;
		break;
	case BIOMETRIC_ATTR_MATCH_THRESHOLD:
		*val = data->last_match_confidence;
		break;
	case BIOMETRIC_ATTR_IMAGE_QUALITY:
		*val = 0;
		break;
	case BIOMETRIC_ATTR_PRIV_START:
		*val = (int32_t)data->last_match_uid;
		break;
	default:
		ret = -ENOTSUP;
		break;
	}

	k_mutex_unlock(&data->lock);
	return ret;
}

static uint32_t dfrobot_timeout_param_ms(struct dfrobot_ai10_data *data, k_timeout_t timeout)
{
	if (K_TIMEOUT_EQ(timeout, K_FOREVER)) {
		return (uint32_t)data->timeout_ms;
	}
	if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
		return 0U;
	}
	return (uint32_t)k_ticks_to_ms_ceil64(timeout.ticks);
}

static int dfrobot_ai10_enroll_start(const struct device *dev, uint16_t template_id)
{
	struct dfrobot_ai10_data *data = dev->data;
	int ret = 0;

	if (template_id == 0U) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->enroll_phase != DFROBOT_ENROLL_IDLE) {
		ret = -EBUSY;
		goto out;
	}

	data->enroll_phase = DFROBOT_ENROLL_WAIT_CAPTURE;
	data->enroll_template_id = template_id;

	LOG_INF("%s: enrollment started (template_id=%u)", dev->name, template_id);

out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int dfrobot_ai10_enroll_capture(const struct device *dev, k_timeout_t timeout,
				       struct biometric_capture_result *result)
{
	struct dfrobot_ai10_data *data = dev->data;
	struct dfrobot_enroll_payload ep;
	uint8_t body[4 + sizeof(ep)];
	uint32_t op_ms;
	struct dfrobot_ai10_reply reply;
	struct dfrobot_ai10_note note;
	int ret;

	if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
		return -EINVAL;
	}

	op_ms = dfrobot_timeout_param_ms(data, timeout);
	if (K_TIMEOUT_EQ(timeout, K_FOREVER)) {
		op_ms = (uint32_t)data->timeout_ms;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->enroll_phase != DFROBOT_ENROLL_WAIT_CAPTURE) {
		ret = -EINVAL;
		goto out;
	}

	memset(&ep, 0, sizeof(ep));
	ep.admin = 0U;
	snprintf((char *)ep.user_name, sizeof(ep.user_name), "U%04u", data->enroll_template_id);
	ep.face_dir = 0U;
	ep.timeout = (uint8_t)MIN(255U, MAX(1U, op_ms / 1000U));

	body[0] = DFROBOT_AI10_ENROLL_SINGLE;
	body[1] = (sizeof(ep) >> 8) & 0xFF;
	body[2] = sizeof(ep) & 0xFF;
	memcpy(&body[3], &ep, sizeof(ep));

	LOG_INF("%s: enroll capture (op_ms=%u, label=%s)", dev->name, op_ms, ep.user_name);

	dfrobot_ai10_uart_drain(data->uart);
	dfrobot_ai10_send_packet(data->uart, body, 3U + sizeof(ep));

	ret = dfrobot_ai10_recv_until_relay(data, op_ms, &reply, &note);
	if (ret != 0) {
		LOG_ERR("%s: enroll capture transport failed: %d", dev->name, ret);
		data->enroll_phase = DFROBOT_ENROLL_IDLE;
		goto out;
	}

	ret = dfrobot_ai10_result_to_errno(reply.result);
	if (ret != 0) {
		LOG_ERR("%s: enroll failed (module result=%u)", dev->name, reply.result);
		data->enroll_phase = DFROBOT_ENROLL_IDLE;
		goto out;
	}

	data->enroll_phase = DFROBOT_ENROLL_DONE;
	LOG_INF("%s: enroll capture succeeded", dev->name);

	if (result != NULL) {
		result->samples_captured = 1U;
		result->samples_required = 1U;
		result->quality = 80U;
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

	if (data->enroll_phase != DFROBOT_ENROLL_DONE) {
		ret = -EINVAL;
		goto out;
	}

	data->enroll_phase = DFROBOT_ENROLL_IDLE;

	LOG_DBG("%s: enrollment finalized", dev->name);

out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int dfrobot_ai10_enroll_abort(const struct device *dev)
{
	struct dfrobot_ai10_data *data = dev->data;

	k_mutex_lock(&data->lock, K_FOREVER);
	data->enroll_phase = DFROBOT_ENROLL_IDLE;
	k_mutex_unlock(&data->lock);

	LOG_INF("%s: enrollment aborted", dev->name);

	return 0;
}

static int dfrobot_ai10_template_delete(const struct device *dev, uint16_t id)
{
	struct dfrobot_ai10_data *data = dev->data;
	uint8_t body[] = {
		DFROBOT_AI10_DEL_USER, 0x00, 0x02, (uint8_t)(id >> 8), (uint8_t)(id & 0xFF),
	};
	struct dfrobot_ai10_reply reply;
	struct dfrobot_ai10_note note;
	int ret;

	if (id == 0U) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	LOG_INF("%s: delete user uid=%u", dev->name, id);

	dfrobot_ai10_uart_drain(data->uart);
	dfrobot_ai10_send_packet(data->uart, body, ARRAY_SIZE(body));

	ret = dfrobot_ai10_recv_until_relay(data, (uint32_t)data->timeout_ms, &reply, &note);
	if (ret != 0) {
		LOG_ERR("%s: delete user failed (transport): %d", dev->name, ret);
		goto out;
	}
	ret = dfrobot_ai10_result_to_errno(reply.result);
	if (ret != 0) {
		LOG_ERR("%s: delete user failed (result=%u)", dev->name, reply.result);
	} else {
		LOG_INF("%s: deleted uid=%u", dev->name, id);
	}

out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int dfrobot_ai10_template_delete_all(const struct device *dev)
{
	struct dfrobot_ai10_data *data = dev->data;
	uint8_t body[] = {DFROBOT_AI10_DEL_ALL_USER, 0x00, 0x00};
	struct dfrobot_ai10_reply reply;
	struct dfrobot_ai10_note note;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	LOG_INF("%s: delete all users", dev->name);

	dfrobot_ai10_uart_drain(data->uart);
	dfrobot_ai10_send_packet(data->uart, body, ARRAY_SIZE(body));

	ret = dfrobot_ai10_recv_until_relay(data, (uint32_t)data->timeout_ms, &reply, &note);
	if (ret != 0) {
		LOG_ERR("%s: delete all failed (transport): %d", dev->name, ret);
		goto out;
	}
	ret = dfrobot_ai10_result_to_errno(reply.result);
	if (ret != 0) {
		LOG_ERR("%s: delete all failed (result=%u)", dev->name, reply.result);
	} else {
		LOG_INF("%s: all users deleted", dev->name);
	}

out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int dfrobot_ai10_template_list(const struct device *dev, uint16_t *ids, size_t max_count,
				      size_t *actual_count)
{
	const struct dfrobot_ai10_config *cfg = dev->config;
	struct dfrobot_ai10_data *data = dev->data;
	uint8_t body[] = {DFROBOT_AI10_GET_ALL_USERID, 0x00, 0x01, 0x00};
	struct dfrobot_ai10_reply reply;
	struct dfrobot_ai10_note note;
	int ret;
	size_t n = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	LOG_DBG("%s: listing user IDs", dev->name);

	dfrobot_ai10_uart_drain(data->uart);
	dfrobot_ai10_send_packet(data->uart, body, ARRAY_SIZE(body));

	ret = dfrobot_ai10_recv_until_relay(data, (uint32_t)data->timeout_ms, &reply, &note);
	if (ret != 0) {
		LOG_ERR("%s: list users failed (transport): %d", dev->name, ret);
		goto out;
	}
	ret = dfrobot_ai10_result_to_errno(reply.result);
	if (ret != 0) {
		LOG_ERR("%s: list users failed (result=%u)", dev->name, reply.result);
		goto out;
	}

	uint8_t user_num = reply.data[0];

	for (uint8_t i = 0; i < user_num && n < max_count && n < cfg->max_users; i++) {
		uint16_t uid =
			(uint16_t)(((uint16_t)reply.data[1 + i * 2] << 8) | reply.data[2 + i * 2]);

		ids[n++] = uid;
	}

	*actual_count = n;
	ret = 0;

	LOG_INF("%s: listed %zu user(s) (module reports %u)", dev->name, n, user_num);

out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int dfrobot_ai10_match(const struct device *dev, enum biometric_match_mode mode,
			      uint16_t template_id, k_timeout_t timeout,
			      struct biometric_match_result *result)
{
	struct dfrobot_ai10_data *data = dev->data;
	uint32_t op_ms = dfrobot_timeout_param_ms(data, timeout);
	uint8_t body[5];
	struct dfrobot_ai10_reply reply;
	struct dfrobot_ai10_note note;
	int ret;

	if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
		return -EINVAL;
	}

	if (mode == BIOMETRIC_MATCH_VERIFY && template_id == 0U) {
		return -EINVAL;
	}

	body[0] = DFROBOT_AI10_VERIFY;
	body[1] = 0x00;
	body[2] = 0x02;
	body[3] = 0x00;
	body[4] = (uint8_t)MIN(255U, MAX(1U, op_ms / 1000U));

	LOG_INF("%s: match mode=%d template_id=%u op_ms=%u", dev->name, mode, template_id, op_ms);

	k_mutex_lock(&data->lock, K_FOREVER);

	dfrobot_ai10_uart_drain(data->uart);
	dfrobot_ai10_send_packet(data->uart, body, ARRAY_SIZE(body));

	ret = dfrobot_ai10_recv_until_relay(data, op_ms, &reply, &note);
	if (ret != 0) {
		LOG_ERR("%s: match failed (transport): %d", dev->name, ret);
		goto out;
	}

	ret = dfrobot_ai10_result_to_errno(reply.result);
	if (ret != 0) {
		LOG_ERR("%s: match failed (module result=%u)", dev->name, reply.result);
		goto out;
	}

	if (reply.mid != DFROBOT_AI10_VERIFY) {
		if (reply.mid == DFROBOT_AI10_MID_SCAN_QR) {
			LOG_WRN("%s: unexpected QR relay response (mid=0x%02x)", dev->name, reply.mid);
			ret = -ENOTSUP;
		} else {
			LOG_ERR("%s: unexpected relay mid=0x%02x (expected VERIFY 0x%02x)", dev->name,
				reply.mid, DFROBOT_AI10_VERIFY);
			ret = -EBADMSG;
		}
		goto out;
	}

	uint16_t uid = (uint16_t)(((uint16_t)reply.data[0] << 8) | reply.data[1]);

	data->last_match_uid = uid;
	data->last_match_confidence = 100;
	data->last_match_modality = dfrobot_uid_modality(uid);

	LOG_INF("%s: match uid=%u modality=%d", dev->name, uid, data->last_match_modality);

	if (mode == BIOMETRIC_MATCH_VERIFY) {
		if (uid != template_id) {
			LOG_WRN("%s: verify mismatch (got uid=%u, expected %u)", dev->name, uid,
				template_id);
			ret = -ENOENT;
			goto out;
		}
	}

	if (result != NULL) {
		result->confidence = 100;
		result->template_id = uid;
		result->image_quality = 0U;
	}

	ret = 0;

out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static DEVICE_API(biometric, biometrics_dfrobot_ai10_api) = {
	.get_capabilities = dfrobot_ai10_get_capabilities,
	.attr_set = dfrobot_ai10_attr_set,
	.attr_get = dfrobot_ai10_attr_get,
	.enroll_start = dfrobot_ai10_enroll_start,
	.enroll_capture = dfrobot_ai10_enroll_capture,
	.enroll_finalize = dfrobot_ai10_enroll_finalize,
	.enroll_abort = dfrobot_ai10_enroll_abort,
	.template_store = NULL,
	.template_read = NULL,
	.template_delete = dfrobot_ai10_template_delete,
	.template_delete_all = dfrobot_ai10_template_delete_all,
	.template_list = dfrobot_ai10_template_list,
	.match = dfrobot_ai10_match,
	.led_control = NULL,
};

static int dfrobot_ai10_init(const struct device *dev)
{
	const struct dfrobot_ai10_config *cfg = dev->config;
	struct dfrobot_ai10_data *data = dev->data;
	int ret;

	if (!device_is_ready(cfg->uart)) {
		LOG_ERR("UART not ready");
		return -ENODEV;
	}

	data->uart = cfg->uart;
	data->max_users = cfg->max_users;
	data->timeout_ms = (int32_t)CONFIG_DFROBOT_AI10_DEFAULT_TIMEOUT_MS;
	data->enroll_phase = DFROBOT_ENROLL_IDLE;
	data->last_match_uid = 0U;
	data->last_match_confidence = 0;
	data->last_match_modality = BIOMETRIC_TYPE_FACE;

	k_mutex_init(&data->lock);

	ret = dfrobot_ai10_do_reset(data);
	if (ret != 0) {
		return ret;
	}

	LOG_INF("DFRobot AI10 ready (max users %u)", data->max_users);

	return 0;
}

#define DFROBOT_AI10_DEFINE(inst)                                                                  \
	static struct dfrobot_ai10_data dfrobot_ai10_data_##inst;                                  \
                                                                                                   \
	static const struct dfrobot_ai10_config dfrobot_ai10_config_##inst = {                     \
		.uart = DEVICE_DT_GET(DT_INST_BUS(inst)),                                          \
		.max_users = DT_INST_PROP_OR(inst, max_users, 100),                                \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, dfrobot_ai10_init, NULL, &dfrobot_ai10_data_##inst,              \
			      &dfrobot_ai10_config_##inst, POST_KERNEL, CONFIG_BIOMETRICS_INIT_PRIORITY, \
			      &biometrics_dfrobot_ai10_api);

DT_INST_FOREACH_STATUS_OKAY(DFROBOT_AI10_DEFINE)
