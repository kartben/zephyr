/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026
 */

#define DT_DRV_COMPAT dfrobot_id809

#include <zephyr/device.h>
#include <zephyr/drivers/biometrics.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <string.h>

#include "biometrics_id809.h"

LOG_MODULE_REGISTER(id809, CONFIG_BIOMETRICS_LOG_LEVEL);

enum id809_packet_kind {
	ID809_PACKET_KIND_RESPONSE,
	ID809_PACKET_KIND_DATA,
};

struct id809_response_header {
	uint16_t prefix;
	uint8_t sid;
	uint8_t did;
	uint16_t code;
	uint16_t len;
	uint16_t ret;
};

static int id809_validate_id(const struct device *dev, uint16_t id)
{
	const struct id809_config *cfg = dev->config;

	if (id < 1 || id > cfg->max_templates || id > ID809_MAX_TEMPLATE_ID) {
		return -EINVAL;
	}

	return 0;
}

static int id809_errno_from_ret(uint16_t ret)
{
	switch (ret & 0xFF) {
	case ID809_ERR_SUCCESS:
		return 0;
	case ID809_ERR_VERIFY:
	case ID809_ERR_IDENTIFY:
	case ID809_ERR_TMPL_EMPTY:
	case ID809_ERR_ALL_TMPL_EMPTY:
		return -ENOENT;
	case ID809_ERR_TMPL_NOT_EMPTY:
	case ID809_ERR_DUPLICATION_ID:
		return -EEXIST;
	case ID809_ERR_EMPTY_ID_NOEXIST:
		return -ENOSPC;
	case ID809_ERR_INVALID_TMPL_DATA:
	case ID809_ERR_INVALID_TMPL_NO:
	case ID809_ERR_INVALID_PARAM:
	case ID809_ERR_INVALID_BUFFER_ID:
	case ID809_ERR_GEN_COUNT:
		return -EINVAL;
	case ID809_ERR_BAD_QUALITY:
	case ID809_ERR_FP_NOT_DETECTED:
		return -EAGAIN;
	case ID809_ERR_TIMEOUT:
	case ID809_ERR_RECV_TIMEOUT:
		return -ETIMEDOUT;
	case ID809_ERR_NOT_AUTHORIZED:
		return -EACCES;
	case ID809_ERR_RECV_LENGTH:
	case ID809_ERR_RECV_CKS:
		return -EBADMSG;
	case ID809_ERR_FP_CANCEL:
		return -ECANCELED;
	default:
		return -EIO;
	}
}

static uint16_t id809_calc_cmd_checksum(uint16_t cmd, uint16_t len, const uint8_t *payload)
{
	uint16_t cks = 0xFF + (cmd & 0xFF) + (cmd >> 8) + (len & 0xFF) + (len >> 8);

	for (uint16_t i = 0; i < len; i++) {
		cks += payload[i];
	}

	return cks;
}

static uint16_t id809_calc_rsp_checksum(const struct id809_response_header *header,
					const uint8_t *payload, uint16_t payload_len)
{
	uint16_t cks = 0xFF + header->sid + header->did + (header->code & 0xFF) +
		       (header->code >> 8) + (header->len & 0xFF) + (header->len >> 8) +
		       (header->ret & 0xFF) + (header->ret >> 8);

	for (uint16_t i = 0; i < payload_len; i++) {
		cks += payload[i];
	}

	return cks;
}

static int id809_wait_ready(const struct device *dev, uint32_t timeout_ms)
{
	const struct id809_config *cfg = dev->config;
	int64_t deadline = k_uptime_get() + timeout_ms;
	uint8_t token;
	int ret;

	do {
		ret = i2c_read_dt(&cfg->bus, &token, sizeof(token));
		if (ret == 0 && token == ID809_READY_TOKEN) {
			return 0;
		}

		k_sleep(K_MSEC(ID809_I2C_POLL_MS));
	} while (k_uptime_get() < deadline);

	return -ETIMEDOUT;
}

static int id809_read_bytes(const struct device *dev, uint8_t *buf, size_t len)
{
	const struct id809_config *cfg = dev->config;
	size_t offset = 0;
	int ret;

	while (offset < len) {
		size_t chunk = MIN(len - offset, (size_t)ID809_I2C_CHUNK_SIZE);

		ret = i2c_read_dt(&cfg->bus, buf + offset, chunk);
		if (ret < 0) {
			return ret;
		}

		offset += chunk;
	}

	return 0;
}

static int id809_send_packet(const struct device *dev, uint16_t prefix, uint16_t cmd,
			     const uint8_t *payload, uint16_t len)
{
	const struct id809_config *cfg = dev->config;
	struct id809_data *data = dev->data;
	uint8_t *buf = data->packet_buf;
	size_t payload_area = (prefix == ID809_CMD_PREFIX_CODE) ? ID809_CMD_PAYLOAD_SIZE : len;
	size_t packet_len = ID809_CMD_HEADER_SIZE + payload_area + ID809_CKS_SIZE;
	uint16_t checksum;
	int ret;

	if (packet_len > sizeof(data->packet_buf) ||
	    (prefix == ID809_CMD_PREFIX_CODE && len > ID809_CMD_PAYLOAD_SIZE)) {
		return -ENOMEM;
	}

	sys_put_le16(prefix, &buf[0]);
	buf[2] = 0;
	buf[3] = 0;
	sys_put_le16(cmd, &buf[4]);
	sys_put_le16(len, &buf[6]);

	memset(&buf[ID809_CMD_HEADER_SIZE], 0, payload_area);
	if (len > 0U) {
		memcpy(&buf[ID809_CMD_HEADER_SIZE], payload, len);
	}

	checksum = id809_calc_cmd_checksum(cmd, len, payload);
	sys_put_le16(checksum, &buf[ID809_CMD_HEADER_SIZE + payload_area]);

	ret = id809_wait_ready(dev, ID809_READY_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	return i2c_write_dt(&cfg->bus, buf, packet_len);
}

static int id809_read_prefix(const struct device *dev, struct id809_response_header *header,
			     enum id809_packet_kind *kind, uint32_t timeout_ms)
{
	uint8_t ch;
	int state = 0;
	uint8_t hdr[ID809_RCM_HEADER_SIZE - 2U];
	int64_t deadline = k_uptime_get() + timeout_ms;
	int ret;

	while (k_uptime_get() < deadline) {
		ret = i2c_read_dt(&((const struct id809_config *)dev->config)->bus, &ch,
				  sizeof(ch));
		if (ret < 0) {
			k_sleep(K_MSEC(ID809_I2C_POLL_MS));
			continue;
		}

		switch (state) {
		case 0:
			if (ch == 0xAA) {
				state = 1;
			} else if (ch == 0xA5) {
				state = 2;
			}
			break;
		case 1:
			if (ch == 0x55) {
				header->prefix = ID809_RCM_PREFIX_CODE;
				*kind = ID809_PACKET_KIND_RESPONSE;
				goto header_found;
			}
			state = (ch == 0xAA) ? 1 : (ch == 0xA5) ? 2 : 0;
			break;
		case 2:
			if (ch == 0x5A) {
				header->prefix = ID809_RCM_DATA_PREFIX_CODE;
				*kind = ID809_PACKET_KIND_DATA;
				goto header_found;
			}
			state = (ch == 0xAA) ? 1 : (ch == 0xA5) ? 2 : 0;
			break;
		default:
			state = 0;
			break;
		}
	}

	return -ETIMEDOUT;

header_found:
	ret = id809_read_bytes(dev, hdr, sizeof(hdr));
	if (ret < 0) {
		return ret;
	}

	header->sid = hdr[0];
	header->did = hdr[1];
	header->code = sys_get_le16(&hdr[2]);
	header->len = sys_get_le16(&hdr[4]);
	header->ret = sys_get_le16(&hdr[6]);

	return 0;
}

static int id809_recv_packet(const struct device *dev, uint16_t expected_cmd, uint8_t *payload,
			     size_t payload_size, size_t *payload_len, enum id809_packet_kind *kind,
			     uint32_t timeout_ms)
{
	struct id809_data *data = dev->data;
	struct id809_response_header header;
	enum id809_packet_kind packet_kind;
	uint8_t *buf = data->packet_buf;
	size_t read_len;
	uint16_t checksum;
	uint16_t calc;
	int ret;

	ret = id809_read_prefix(dev, &header, &packet_kind, timeout_ms);
	if (ret < 0) {
		return ret;
	}

	if (expected_cmd != 0U && header.code != expected_cmd) {
		return -EBADMSG;
	}

	read_len = (packet_kind == ID809_PACKET_KIND_RESPONSE)
			   ? (ID809_FIXED_RESPONSE_DATA_SIZE + ID809_CKS_SIZE)
			   : header.len;
	if (read_len < ID809_CKS_SIZE || read_len > sizeof(data->packet_buf)) {
		return -EBADMSG;
	}

	ret = id809_read_bytes(dev, buf, read_len);
	if (ret < 0) {
		return ret;
	}

	checksum = sys_get_le16(&buf[read_len - ID809_CKS_SIZE]);
	calc = id809_calc_rsp_checksum(&header, buf, read_len - ID809_CKS_SIZE);
	if (checksum != calc) {
		return -EBADMSG;
	}

	ret = id809_errno_from_ret(header.ret);
	if (ret < 0) {
		return ret;
	}

	if (payload_len != NULL) {
		*payload_len = read_len - ID809_CKS_SIZE;
	}

	if (kind != NULL) {
		*kind = packet_kind;
	}

	if (payload != NULL) {
		size_t copy_len = read_len - ID809_CKS_SIZE;

		if (copy_len > payload_size) {
			return -EMSGSIZE;
		}

		memcpy(payload, buf, copy_len);
	}

	return 0;
}

static int id809_command(const struct device *dev, uint16_t cmd, const uint8_t *payload,
			 uint16_t len, uint8_t *rsp_payload, size_t rsp_size, size_t *rsp_len,
			 uint32_t timeout_ms)
{
	enum id809_packet_kind kind;
	int ret;

	ret = id809_send_packet(dev, ID809_CMD_PREFIX_CODE, cmd, payload, len);
	if (ret < 0) {
		return ret;
	}

	ret = id809_recv_packet(dev, cmd, rsp_payload, rsp_size, rsp_len, &kind, timeout_ms);
	if (ret < 0) {
		return ret;
	}

	return (kind == ID809_PACKET_KIND_RESPONSE) ? 0 : -EBADMSG;
}

static uint32_t id809_timeout_to_ms(const struct id809_data *data, k_timeout_t timeout)
{
	if (K_TIMEOUT_EQ(timeout, K_FOREVER)) {
		return data->timeout_ms;
	}

	if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
		return 0U;
	}

	return (uint32_t)k_ticks_to_ms_ceil64(timeout.ticks);
}

static int id809_set_param_internal(const struct device *dev, uint8_t param, uint32_t value)
{
	uint8_t cmd[5] = {param, 0, 0, 0, 0};

	sys_put_le32(value, &cmd[1]);

	return id809_command(dev, ID809_CMD_SET_PARAM, cmd, sizeof(cmd), NULL, 0, NULL,
			     ID809_RESPONSE_TIMEOUT_MS);
}

static int id809_get_param_internal(const struct device *dev, uint8_t param, uint8_t *value)
{
	uint8_t cmd = param;
	uint8_t rsp[ID809_FIXED_RESPONSE_DATA_SIZE];
	size_t rsp_len;
	int ret;

	ret = id809_command(dev, ID809_CMD_GET_PARAM, &cmd, sizeof(cmd), rsp, sizeof(rsp), &rsp_len,
			    ID809_RESPONSE_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	if (rsp_len < 1U) {
		return -EBADMSG;
	}

	*value = rsp[0];
	return 0;
}

static int id809_detect_finger_internal(const struct device *dev, bool *present)
{
	uint8_t rsp[ID809_FIXED_RESPONSE_DATA_SIZE];
	size_t rsp_len;
	int ret;

	ret = id809_command(dev, ID809_CMD_FINGER_DETECT, NULL, 0, rsp, sizeof(rsp), &rsp_len,
			    ID809_RESPONSE_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	if (rsp_len < 1U) {
		return -EBADMSG;
	}

	*present = (rsp[0] != 0U);
	return 0;
}

static int id809_wait_for_finger(const struct device *dev, uint32_t timeout_ms)
{
	int64_t deadline = k_uptime_get() + timeout_ms;
	bool present = false;
	int ret;

	do {
		ret = id809_detect_finger_internal(dev, &present);
		if (ret < 0) {
			if (ret == -EAGAIN) {
				present = false;
			} else {
				return ret;
			}
		}

		if (present) {
			return 0;
		}

		if (timeout_ms == 0U) {
			break;
		}

		k_sleep(K_MSEC(ID809_I2C_POLL_MS));
	} while (k_uptime_get() < deadline);

	return -ETIMEDOUT;
}

static int id809_generate_internal(const struct device *dev, uint8_t buffer_id)
{
	uint8_t cmd[2] = {buffer_id, 0};

	return id809_command(dev, ID809_CMD_GENERATE, cmd, sizeof(cmd), NULL, 0, NULL,
			     ID809_RESPONSE_TIMEOUT_MS);
}

static int id809_capture_to_buffer(const struct device *dev, uint8_t buffer_id, uint32_t timeout_ms)
{
	int ret;

	ret = id809_wait_for_finger(dev, timeout_ms);
	if (ret < 0) {
		return ret;
	}

	ret = id809_command(dev, ID809_CMD_GET_IMAGE, NULL, 0, NULL, 0, NULL,
			    timeout_ms + ID809_RESPONSE_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	return id809_generate_internal(dev, buffer_id);
}

static int id809_merge_internal(const struct device *dev, uint8_t samples)
{
	uint8_t cmd[3] = {0, 0, samples};

	return id809_command(dev, ID809_CMD_MERGE, cmd, sizeof(cmd), NULL, 0, NULL,
			     ID809_RESPONSE_TIMEOUT_MS);
}

static int id809_store_internal(const struct device *dev, uint16_t id)
{
	uint8_t cmd[4] = {(uint8_t)id, 0, 0, 0};

	return id809_command(dev, ID809_CMD_STORE_CHAR, cmd, sizeof(cmd), NULL, 0, NULL,
			     ID809_RESPONSE_TIMEOUT_MS);
}

static int id809_load_internal(const struct device *dev, uint16_t id, uint8_t buffer_id)
{
	uint8_t cmd[4] = {(uint8_t)id, 0, buffer_id, 0};

	return id809_command(dev, ID809_CMD_LOAD_CHAR, cmd, sizeof(cmd), NULL, 0, NULL,
			     ID809_RESPONSE_TIMEOUT_MS);
}

static int id809_get_capabilities(const struct device *dev, struct biometric_capabilities *caps)
{
	const struct id809_config *cfg = dev->config;

	caps->type = BIOMETRIC_TYPE_FINGERPRINT;
	caps->max_templates = cfg->max_templates;
	caps->template_size = cfg->template_size;
	caps->storage_modes = BIOMETRIC_STORAGE_DEVICE;
	caps->enrollment_samples_required = ID809_ENROLL_SAMPLES;

	return 0;
}

static int id809_attr_set(const struct device *dev, enum biometric_attribute attr, int32_t val)
{
	struct id809_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	switch (attr) {
	case BIOMETRIC_ATTR_MATCH_THRESHOLD:
		data->match_threshold = val;
		ret = 0;
		break;
	case BIOMETRIC_ATTR_ENROLLMENT_QUALITY:
		data->enroll_quality = val;
		ret = 0;
		break;
	case BIOMETRIC_ATTR_SECURITY_LEVEL:
		if (val < 1 || val > 10) {
			ret = -EINVAL;
			break;
		}
		ret = id809_set_param_internal(dev, ID809_PARAM_SECURITY_LEVEL,
					       CLAMP((val + 1) / 2, 1, 5));
		if (ret == 0) {
			data->security_level = val;
		}
		break;
	case BIOMETRIC_ATTR_TIMEOUT_MS:
		if (val < 0 || val > ID809_MAX_TIMEOUT_MS) {
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
	case BIOMETRIC_ATTR_MATCH_THRESHOLD:
		*val = data->match_threshold;
		break;
	case BIOMETRIC_ATTR_ENROLLMENT_QUALITY:
		*val = data->enroll_quality;
		break;
	case BIOMETRIC_ATTR_SECURITY_LEVEL:
		*val = data->security_level;
		break;
	case BIOMETRIC_ATTR_TIMEOUT_MS:
		*val = (int32_t)data->timeout_ms;
		break;
	case BIOMETRIC_ATTR_IMAGE_QUALITY:
		ret = -ENOTSUP;
		break;
	case BIOMETRIC_ATTR_PRIV_START:
		*val = data->last_match_id;
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
	int ret;

	ret = id809_validate_id(dev, template_id);
	if (ret < 0) {
		return ret;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	if (data->enroll_state != ID809_ENROLL_IDLE) {
		k_mutex_unlock(&data->lock);
		return -EBUSY;
	}

	data->enroll_state = ID809_ENROLL_WAIT_SAMPLE_1;
	data->enroll_id = template_id;
	k_mutex_unlock(&data->lock);

	return 0;
}

static int id809_enroll_capture(const struct device *dev, k_timeout_t timeout,
				struct biometric_capture_result *result)
{
	struct id809_data *data = dev->data;
	enum id809_enroll_state state;
	uint32_t timeout_ms;
	uint8_t sample_idx;
	int ret;

	timeout_ms = id809_timeout_to_ms(data, timeout);
	if (timeout_ms > ID809_MAX_TIMEOUT_MS) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	state = data->enroll_state;
	if (state == ID809_ENROLL_IDLE) {
		k_mutex_unlock(&data->lock);
		return -EINVAL;
	}
	if (state == ID809_ENROLL_READY) {
		k_mutex_unlock(&data->lock);
		return -EALREADY;
	}

	sample_idx = (uint8_t)(state - ID809_ENROLL_WAIT_SAMPLE_1);
	ret = id809_capture_to_buffer(dev, sample_idx, timeout_ms);
	if (ret < 0) {
		data->enroll_state = ID809_ENROLL_IDLE;
		k_mutex_unlock(&data->lock);
		return ret;
	}

	data->enroll_state = (state == ID809_ENROLL_WAIT_SAMPLE_3)
				     ? ID809_ENROLL_READY
				     : (enum id809_enroll_state)(state + 1);
	data->image_quality = 0;

	if (result != NULL) {
		result->samples_captured = sample_idx + 1U;
		result->samples_required = ID809_ENROLL_SAMPLES;
		result->quality = 0U;
	}

	k_mutex_unlock(&data->lock);
	return 0;
}

static int id809_enroll_finalize(const struct device *dev)
{
	struct id809_data *data = dev->data;
	uint16_t enroll_id;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	if (data->enroll_state != ID809_ENROLL_READY) {
		k_mutex_unlock(&data->lock);
		return -EINVAL;
	}

	enroll_id = data->enroll_id;
	ret = id809_merge_internal(dev, ID809_ENROLL_SAMPLES);
	if (ret == 0) {
		ret = id809_store_internal(dev, enroll_id);
	}
	data->enroll_state = ID809_ENROLL_IDLE;
	k_mutex_unlock(&data->lock);

	return ret;
}

static int id809_enroll_abort(const struct device *dev)
{
	struct id809_data *data = dev->data;
	bool was_idle;

	k_mutex_lock(&data->lock, K_FOREVER);
	was_idle = (data->enroll_state == ID809_ENROLL_IDLE);
	data->enroll_state = ID809_ENROLL_IDLE;
	k_mutex_unlock(&data->lock);

	return was_idle ? -EALREADY : 0;
}

static int id809_template_store(const struct device *dev, uint16_t id, const uint8_t *tpl,
				size_t size)
{
	const struct id809_config *cfg = dev->config;
	struct id809_data *data = dev->data;
	uint8_t *buf = data->packet_buf;
	size_t payload_len;
	uint16_t transfer_len;
	int ret;

	ret = id809_validate_id(dev, id);
	if (ret < 0) {
		return ret;
	}
	if (size != cfg->template_size) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	transfer_len = cfg->template_size + ID809_TEMPLATE_PREFIX_SIZE;
	sys_put_le16(transfer_len, buf);
	ret = id809_command(dev, ID809_CMD_DOWN_CHAR, buf, 2U, NULL, 0, NULL,
			    ID809_RESPONSE_TIMEOUT_MS);
	if (ret < 0) {
		goto out;
	}

	buf[0] = 0;
	buf[1] = 0;
	memcpy(&buf[2], tpl, cfg->template_size);
	ret = id809_send_packet(dev, ID809_CMD_DATA_PREFIX_CODE, ID809_CMD_DOWN_CHAR, buf,
				transfer_len);
	if (ret < 0) {
		goto out;
	}

	ret = id809_recv_packet(dev, ID809_CMD_DOWN_CHAR, NULL, 0, &payload_len, NULL,
				ID809_RESPONSE_TIMEOUT_MS);
	if (ret < 0) {
		goto out;
	}

	ret = id809_store_internal(dev, id);
out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int id809_template_read(const struct device *dev, uint16_t id, uint8_t *tpl, size_t size)
{
	const struct id809_config *cfg = dev->config;
	struct id809_data *data = dev->data;
	size_t payload_len;
	enum id809_packet_kind kind;
	int ret;

	ret = id809_validate_id(dev, id);
	if (ret < 0) {
		return ret;
	}
	if (size < cfg->template_size) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = id809_load_internal(dev, id, 0U);
	if (ret < 0) {
		goto out;
	}

	ret = id809_command(dev, ID809_CMD_UP_CHAR, (const uint8_t[]){0, 0}, 2U, NULL, 0, NULL,
			    ID809_RESPONSE_TIMEOUT_MS);
	if (ret < 0) {
		goto out;
	}

	ret = id809_recv_packet(dev, ID809_CMD_UP_CHAR, data->packet_buf, sizeof(data->packet_buf),
				&payload_len, &kind, ID809_RESPONSE_TIMEOUT_MS);
	if (ret < 0) {
		goto out;
	}
	if (kind != ID809_PACKET_KIND_DATA || payload_len < cfg->template_size + 2U) {
		ret = -EBADMSG;
		goto out;
	}

	memcpy(tpl, &data->packet_buf[2], cfg->template_size);
	ret = cfg->template_size;
out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int id809_template_delete(const struct device *dev, uint16_t id)
{
	uint8_t cmd[4] = {(uint8_t)id, 0, (uint8_t)id, 0};
	struct id809_data *data = dev->data;
	int ret;

	ret = id809_validate_id(dev, id);
	if (ret < 0) {
		return ret;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = id809_command(dev, ID809_CMD_DEL_CHAR, cmd, sizeof(cmd), NULL, 0, NULL,
			    ID809_RESPONSE_TIMEOUT_MS);
	k_mutex_unlock(&data->lock);

	return ret;
}

static int id809_template_delete_all(const struct device *dev)
{
	const struct id809_config *cfg = dev->config;
	struct id809_data *data = dev->data;
	uint8_t cmd[4] = {1, 0, (uint8_t)cfg->max_templates, 0};
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = id809_command(dev, ID809_CMD_DEL_CHAR, cmd, sizeof(cmd), NULL, 0, NULL,
			    ID809_RESPONSE_TIMEOUT_MS);
	k_mutex_unlock(&data->lock);

	return ret;
}

static int id809_template_list(const struct device *dev, uint16_t *ids, size_t max_count,
			       size_t *actual_count)
{
	const struct id809_config *cfg = dev->config;
	struct id809_data *data = dev->data;
	uint8_t rsp[ID809_FIXED_RESPONSE_DATA_SIZE];
	size_t rsp_len;
	size_t payload_len;
	enum id809_packet_kind kind;
	size_t count = 0;
	uint16_t bitmap_len;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = id809_command(dev, ID809_CMD_GET_ENROLLED_ID_LIST, NULL, 0, rsp, sizeof(rsp),
			    &rsp_len, ID809_RESPONSE_TIMEOUT_MS);
	if (ret == -ENOENT) {
		*actual_count = 0;
		ret = 0;
		goto out;
	}
	if (ret < 0) {
		goto out;
	}
	if (rsp_len < 2U) {
		ret = -EBADMSG;
		goto out;
	}

	bitmap_len = sys_get_le16(rsp);
	ret = id809_recv_packet(dev, ID809_CMD_GET_ENROLLED_ID_LIST, data->packet_buf,
				sizeof(data->packet_buf), &payload_len, &kind,
				ID809_RESPONSE_TIMEOUT_MS);
	if (ret < 0) {
		goto out;
	}
	if (kind != ID809_PACKET_KIND_DATA || payload_len < bitmap_len) {
		ret = -EBADMSG;
		goto out;
	}

	for (uint16_t byte_idx = 0; byte_idx < bitmap_len && count < max_count; byte_idx++) {
		uint8_t byte = data->packet_buf[byte_idx];

		for (uint8_t bit = 0; bit < 8U && count < max_count; bit++) {
			uint16_t id = (byte_idx * 8U) + bit + 1U;

			if (id > cfg->max_templates) {
				break;
			}
			if ((byte & BIT(bit)) != 0U) {
				ids[count++] = id;
			}
		}
	}

	*actual_count = count;
	ret = 0;
out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int id809_match(const struct device *dev, enum biometric_match_mode mode,
		       uint16_t template_id, k_timeout_t timeout,
		       struct biometric_match_result *result)
{
	const struct id809_config *cfg = dev->config;
	struct id809_data *data = dev->data;
	uint32_t timeout_ms = id809_timeout_to_ms(data, timeout);
	uint8_t cmd[6] = {0};
	uint8_t rsp[ID809_FIXED_RESPONSE_DATA_SIZE];
	size_t rsp_len;
	int ret;

	if (timeout_ms > ID809_MAX_TIMEOUT_MS) {
		return -EINVAL;
	}
	if (mode == BIOMETRIC_MATCH_VERIFY) {
		ret = id809_validate_id(dev, template_id);
		if (ret < 0) {
			return ret;
		}
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = id809_capture_to_buffer(dev, 0U, timeout_ms);
	if (ret < 0) {
		goto out;
	}

	if (mode == BIOMETRIC_MATCH_VERIFY) {
		cmd[0] = (uint8_t)template_id;
		ret = id809_command(dev, ID809_CMD_VERIFY, cmd, 4U, rsp, sizeof(rsp), &rsp_len,
				    ID809_RESPONSE_TIMEOUT_MS);
		if (ret < 0) {
			goto out;
		}
		data->last_match_id = template_id;
	} else {
		cmd[2] = 1U;
		cmd[4] = (uint8_t)cfg->max_templates;
		ret = id809_command(dev, ID809_CMD_SEARCH, cmd, sizeof(cmd), rsp, sizeof(rsp),
				    &rsp_len, ID809_RESPONSE_TIMEOUT_MS);
		if (ret < 0) {
			goto out;
		}
		if (rsp_len < 1U || rsp[0] < 1U || rsp[0] > cfg->max_templates) {
			ret = -EBADMSG;
			goto out;
		}
		data->last_match_id = rsp[0];
	}

	if (result != NULL) {
		result->confidence = 100;
		result->template_id =
			(mode == BIOMETRIC_MATCH_IDENTIFY) ? data->last_match_id : template_id;
		result->image_quality = 0U;
	}
	ret = 0;
out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static void id809_fill_led_cmd(const struct device *dev, enum biometric_led_state state,
			       uint8_t *cmd)
{
	const struct id809_config *cfg = dev->config;
	bool legacy_map = (cfg->max_templates > 80U);
	uint8_t mode;
	uint8_t color;

	switch (state) {
	case BIOMETRIC_LED_OFF:
		mode = ID809_LED_MODE_OFF;
		color = ID809_LED_COLOR_GREEN;
		break;
	case BIOMETRIC_LED_ON:
		mode = ID809_LED_MODE_ON;
		color = ID809_LED_COLOR_GREEN;
		break;
	case BIOMETRIC_LED_BLINK:
		mode = ID809_LED_MODE_FAST_BLINK;
		color = ID809_LED_COLOR_BLUE;
		break;
	case BIOMETRIC_LED_BREATHE:
		mode = ID809_LED_MODE_BREATHING;
		color = ID809_LED_COLOR_BLUE;
		break;
	default:
		mode = ID809_LED_MODE_OFF;
		color = ID809_LED_COLOR_GREEN;
		break;
	}

	if (!legacy_map) {
		cmd[0] = mode;
		cmd[1] = color;
		cmd[2] = color;
		cmd[3] = 0;
		return;
	}

	switch (mode) {
	case ID809_LED_MODE_BREATHING:
		cmd[0] = 2U;
		break;
	case ID809_LED_MODE_FAST_BLINK:
		cmd[0] = 4U;
		break;
	case ID809_LED_MODE_ON:
		cmd[0] = 1U;
		break;
	case ID809_LED_MODE_OFF:
	default:
		cmd[0] = 0U;
		break;
	}

	switch (color) {
	case ID809_LED_COLOR_GREEN:
		cmd[1] = 0x84U;
		break;
	case ID809_LED_COLOR_RED:
		cmd[1] = 0x82U;
		break;
	case ID809_LED_COLOR_BLUE:
	default:
		cmd[1] = 0x81U;
		break;
	}

	cmd[2] = cmd[1];
	cmd[3] = 0U;
}

static int id809_led_control(const struct device *dev, enum biometric_led_state state)
{
	struct id809_data *data = dev->data;
	uint8_t cmd[4];
	int ret;

	if ((uint32_t)state > BIOMETRIC_LED_BREATHE) {
		return -EINVAL;
	}

	id809_fill_led_cmd(dev, state, cmd);

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = id809_command(dev, ID809_CMD_SLED_CTRL, cmd, sizeof(cmd), NULL, 0, NULL,
			    ID809_RESPONSE_TIMEOUT_MS);
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
	.template_store = id809_template_store,
	.template_read = id809_template_read,
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
	uint8_t security_level;
	int ret;

	if (!i2c_is_ready_dt(&cfg->bus)) {
		return -ENODEV;
	}
	if (cfg->template_size > ID809_MAX_TEMPLATE_SIZE) {
		return -EINVAL;
	}

	k_mutex_init(&data->lock);
	data->enroll_state = ID809_ENROLL_IDLE;
	data->match_threshold = 0;
	data->enroll_quality = 0;
	data->security_level = 6;
	data->timeout_ms = ID809_RESPONSE_TIMEOUT_MS;
	data->image_quality = 0;
	data->last_match_id = 0;
	data->led_state = BIOMETRIC_LED_OFF;

	ret = id809_command(dev, ID809_CMD_TEST_CONNECTION, NULL, 0, NULL, 0, NULL,
			    ID809_RESPONSE_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	ret = id809_get_param_internal(dev, ID809_PARAM_SECURITY_LEVEL, &security_level);
	if (ret == 0) {
		data->security_level = CLAMP((int32_t)security_level * 2, 1, 10);
	}

	return 0;
}

#define ID809_DEFINE(inst)                                                                         \
	static struct id809_data id809_data_##inst;                                                \
	static const struct id809_config id809_config_##inst = {                                   \
		.bus = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.max_templates = DT_INST_PROP(inst, max_templates),                                \
		.template_size = DT_INST_PROP(inst, template_size),                                \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, id809_init, NULL, &id809_data_##inst, &id809_config_##inst,    \
			      POST_KERNEL, CONFIG_BIOMETRICS_INIT_PRIORITY,                        \
			      &biometrics_id809_api);

DT_INST_FOREACH_STATUS_OKAY(ID809_DEFINE)
