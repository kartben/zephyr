/*
 * Copyright (c) 2026 Alif Semiconductor.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ams_ens210

#include <stdint.h>

#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_clock.h>
#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/logging/log.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/sys/mpsc_lockfree.h>
#include <zephyr/sys/byteorder.h>

#include "ens210.h"

LOG_MODULE_DECLARE(ENS210, CONFIG_SENSOR_LOG_LEVEL);

#define ENS210_CONTINUOUS \
	(IS_ENABLED(CONFIG_ENS210_TEMPERATURE_CONTINUOUS) || \
	 IS_ENABLED(CONFIG_ENS210_HUMIDITY_CONTINUOUS))


static void ens210_start_next(struct ens210_data *data);

static struct rtio_iodev_sqe *ens210_finish_slot(struct ens210_data *data)
{
	struct rtio_iodev_sqe *sqe;
	k_spinlock_key_t key;

	key = k_spin_lock(&data->mpsc_lock);
	sqe = data->pending_sqe;
	data->pending_sqe = NULL;
	k_spin_unlock(&data->mpsc_lock, key);

	return sqe;
}

static void ens210_finish_err(struct ens210_data *data, int err)
{
	struct rtio_iodev_sqe *iodev_sqe = ens210_finish_slot(data);

	if (iodev_sqe != NULL) {
		rtio_iodev_sqe_err(iodev_sqe, err);
	}
	ens210_start_next(data);
}

static void ens210_finish_ok(struct ens210_data *data,
				 const struct ens210_value_data *temp,
				 const struct ens210_value_data *humidity)
{
	struct rtio_iodev_sqe *iodev_sqe = ens210_finish_slot(data);
	struct ens210_encoded_data *edata;
	uint8_t *buf;
	uint32_t buf_len;
	uint64_t cycles;
	int rc;

	if (iodev_sqe == NULL) {
		ens210_start_next(data);
		return;
	}

	rc = rtio_sqe_rx_buf(iodev_sqe, sizeof(*edata), sizeof(*edata),
				 &buf, &buf_len);
	if (rc != 0) {
		rtio_iodev_sqe_err(iodev_sqe, rc);
		ens210_start_next(data);
		return;
	}

	rc = sensor_clock_get_cycles(&cycles);
	if (rc != 0) {
		rtio_iodev_sqe_err(iodev_sqe, rc);
		ens210_start_next(data);
		return;
	}

	edata = (struct ens210_encoded_data *)buf;
	edata->header.base_timestamp_ns = sensor_clock_cycles_to_ns(cycles);
	edata->header.reading_count = 1U;
	edata->temp = *temp;
	edata->humidity = *humidity;

	rtio_iodev_sqe_ok(iodev_sqe, 0);
	ens210_start_next(data);
}

#if ENS210_CONTINUOUS

static void ens210_complete_cb_cont(struct rtio *r,
					const struct rtio_sqe *sqe, int res, void *arg)
{
	const struct device *dev = arg;
	struct ens210_data *data = dev->data;
	const struct ens210_value_data *raw =
		(const struct ens210_value_data *)&data->raw_buffer[1];
	const struct sensor_read_config *cfg =
				data->pending_sqe->sqe.iodev->data;
	bool temp_valid = false, hum_valid = false;

	ARG_UNUSED(r);
	ARG_UNUSED(sqe);

	if (res < 0) {
		ens210_finish_err(data, res);
		return;
	}

	for (size_t i = 0; i < cfg->count; i++) {
		if (cfg->channels[i].chan_type == SENSOR_CHAN_ALL ||
			cfg->channels[i].chan_type == SENSOR_CHAN_AMBIENT_TEMP) {
			temp_valid = true;
		}
		if (cfg->channels[i].chan_type == SENSOR_CHAN_ALL ||
			cfg->channels[i].chan_type == SENSOR_CHAN_HUMIDITY) {
			hum_valid = true;
		}
	}

	if (temp_valid && ens210_check_value(&raw[0]) < 0) {
		ens210_finish_err(data, -EIO);
		return;
	}

	if (hum_valid && ens210_check_value(&raw[1]) < 0) {
		ens210_finish_err(data, -EIO);
		return;
	}

	ens210_finish_ok(data, &raw[0], &raw[1]);
}

static void ens210_start_transfer_cont(struct ens210_data *data,
					   struct rtio_iodev_sqe *iodev_sqe)
{
	struct rtio_sqe *wr, *rd, *cb;

	data->raw_buffer[0] = ENS210_REG_T_VAL;

	wr = rtio_sqe_acquire(data->r);
	rd = rtio_sqe_acquire(data->r);
	cb = rtio_sqe_acquire(data->r);

	if ((wr == NULL) || (rd == NULL) || (cb == NULL)) {
		rtio_sqe_drop_all(data->r);
		ens210_finish_err(data, -ENOMEM);
		return;
	}

	rtio_sqe_prep_tiny_write(wr, data->bus_iodev, RTIO_PRIO_NORM,
				 data->raw_buffer, 1, NULL);
	wr->flags = RTIO_SQE_TRANSACTION;

	rtio_sqe_prep_read(rd, data->bus_iodev, RTIO_PRIO_NORM,
			   &data->raw_buffer[1], 6, NULL);
	rd->flags = RTIO_SQE_CHAINED;
	rd->iodev_flags |= RTIO_IODEV_I2C_STOP | RTIO_IODEV_I2C_RESTART;

	rtio_sqe_prep_callback_no_cqe(cb, ens210_complete_cb_cont,
					  (void *)(uintptr_t)data->dev, iodev_sqe);

	rtio_submit(data->r, 0);
}

#else /* !ENS210_CONTINUOUS */



static void ens210_complete_cb_ss(struct rtio *r, const struct rtio_sqe *sqe,
						int res, void *arg)
{
	const struct device *dev = arg;
	struct ens210_data *data = dev->data;
	const struct ens210_value_data *raw =
		(const struct ens210_value_data *)&data->raw_buffer[1];
	const struct sensor_read_config *cfg =
		data->pending_sqe->sqe.iodev->data;
	bool temp_valid = false, hum_valid = false;

	ARG_UNUSED(r);
	ARG_UNUSED(sqe);

	if (res < 0) {
		ens210_finish_err(data, res);
		return;
	}

	for (size_t i = 0; i < cfg->count; i++) {
		if (cfg->channels[i].chan_type == SENSOR_CHAN_ALL ||
			cfg->channels[i].chan_type == SENSOR_CHAN_AMBIENT_TEMP) {
			temp_valid = true;
		}
		if (cfg->channels[i].chan_type == SENSOR_CHAN_ALL ||
			cfg->channels[i].chan_type == SENSOR_CHAN_HUMIDITY) {
			hum_valid = true;
		}
	}

	if (temp_valid && ens210_check_value(&raw[0]) < 0) {
		ens210_finish_err(data, -EIO);
		return;
	}
	if (hum_valid && ens210_check_value(&raw[1]) < 0) {
		ens210_finish_err(data, -EIO);
		return;
	}

	ens210_finish_ok(data, &raw[0], &raw[1]);
}

static void ens210_start_transfer_ss(struct ens210_data *data,
					 struct rtio_iodev_sqe *iodev_sqe)
{
	struct rtio_sqe *wr_start, *dly, *wr_reg, *rd, *cb;

	/*
	 * raw_buffer layout:
	 *   [0]    : register address byte (reused: SENS_START then T_VAL)
	 *   [1..6] : 6-byte read result (T_VAL + H_VAL)
	 */
	data->raw_buffer[0] = ENS210_REG_SENS_START;
	data->raw_buffer[1] = (ENS210_T_START << 0) | (ENS210_H_START << 1);

	wr_start = rtio_sqe_acquire(data->r);
	dly      = rtio_sqe_acquire(data->r);
	wr_reg   = rtio_sqe_acquire(data->r);
	rd       = rtio_sqe_acquire(data->r);
	cb       = rtio_sqe_acquire(data->r);

	if ((wr_start == NULL) || (dly == NULL) || (wr_reg == NULL) ||
		(rd == NULL) || (cb == NULL)) {
		rtio_sqe_drop_all(data->r);
		ens210_finish_err(data, -ENOMEM);
		return;
	}

	/* 1. Write SENS_START to trigger single-shot measurement */
	rtio_sqe_prep_tiny_write(wr_start, data->bus_iodev, RTIO_PRIO_NORM,
				 data->raw_buffer, 2, NULL);
	wr_start->flags = RTIO_SQE_CHAINED;

	/* 2. Delay 130 ms for conversion — replaces k_work_delayable */
	rtio_sqe_prep_delay(dly, K_MSEC(ENS210_CONVERSION_TIME_MS), NULL);
	dly->flags = RTIO_SQE_CHAINED;

	/* 3. Write T_VAL register address (I2C repeated-start write phase) */
	data->raw_buffer[0] = ENS210_REG_T_VAL;
	rtio_sqe_prep_tiny_write(wr_reg, data->bus_iodev, RTIO_PRIO_NORM,
				 data->raw_buffer, 1, NULL);
	wr_reg->flags = RTIO_SQE_TRANSACTION;

	/* 4. Read 6 bytes: T_VAL (3 B) + H_VAL (3 B) */
	rtio_sqe_prep_read(rd, data->bus_iodev, RTIO_PRIO_NORM,
			   &data->raw_buffer[1], 6, NULL);
	rd->flags = RTIO_SQE_CHAINED;
	rd->iodev_flags |= RTIO_IODEV_I2C_STOP | RTIO_IODEV_I2C_RESTART;

	/* 5. Completion callback */
	rtio_sqe_prep_callback_no_cqe(cb, ens210_complete_cb_ss,
					(void *)(uintptr_t)data->dev, iodev_sqe);

	rtio_submit(data->r, 0);
}

#endif /* ENS210_CONTINUOUS */

static void ens210_start_next(struct ens210_data *data)
{
	k_spinlock_key_t key = k_spin_lock(&data->mpsc_lock);

	if (data->pending_sqe != NULL) {
		k_spin_unlock(&data->mpsc_lock, key);
		return;
	}

	struct mpsc_node *node = mpsc_pop(&data->io_q);

	if (node == NULL) {
		k_spin_unlock(&data->mpsc_lock, key);
		return;
	}

	struct rtio_iodev_sqe *next_sqe =
		CONTAINER_OF(node, struct rtio_iodev_sqe, q);

	data->pending_sqe = next_sqe;
	k_spin_unlock(&data->mpsc_lock, key);

#if ENS210_CONTINUOUS
	ens210_start_transfer_cont(data, next_sqe);
#else
	ens210_start_transfer_ss(data, next_sqe);
#endif
}

static int ens210_validate_request(const struct sensor_read_config *cfg)
{
	if (cfg->is_streaming) {
		return -ENOTSUP;
	}

	for (size_t i = 0; i < cfg->count; i++) {
		if (cfg->channels[i].chan_idx != 0) {
			return -ENOTSUP;
		}

		switch (cfg->channels[i].chan_type) {
		case SENSOR_CHAN_ALL:
		case SENSOR_CHAN_AMBIENT_TEMP:
		case SENSOR_CHAN_HUMIDITY:
			break;
		default:
			return -ENOTSUP;
		}
	}

	return 0;
}

void ens210_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe)
{
	const struct sensor_read_config *cfg = iodev_sqe->sqe.iodev->data;
	struct ens210_data *data = dev->data;
	int ret;

	ret = ens210_validate_request(cfg);
	if (ret < 0) {
		rtio_iodev_sqe_err(iodev_sqe, ret);
		return;
	}

	/* Always push to queue first (any context can call this) */
	mpsc_push(&data->io_q, &iodev_sqe->q);

	/* Then try to start next with spinlock protection */
	ens210_start_next(data);
}

/*
 * T_VAL and H_VAL registers: 16-bit unsigned value, little-endian, followed by a byte holding
 * the valid bit (bit 0) and the CRC. T_VAL is in 1/64 K, H_VAL in 1/512 %RH.
 */
#define ENS210_VALUE_SIZE  sizeof(struct ens210_value_data)
#define ENS210_VALUE_VALID BIT(0)
#define ENS210_T_VAL_POS   0U
#define ENS210_H_VAL_POS   ENS210_VALUE_SIZE
#define ENS210_FRAME_SIZE  (2U * ENS210_VALUE_SIZE)

#define ENS210_TEMP_SCALE    SENSOR_Q31_SCALE(1, 64, ENS210_TEMP_SHIFT)
#define ENS210_HUM_SCALE     SENSOR_Q31_SCALE(1, 512, ENS210_HUMIDITY_SHIFT)
#define ENS210_KELVIN_OFFSET SENSOR_Q31_SCALE(27315, 100, ENS210_TEMP_SHIFT)

/* The raw values are unsigned: convert them as 17-bit two's complement values */
#define ENS210_VALUE_BITS 17U

static bool ens210_chan_is_supported(struct sensor_chan_spec chan_spec)
{
	return chan_spec.chan_idx == 0U && (chan_spec.chan_type == SENSOR_CHAN_AMBIENT_TEMP ||
					    chan_spec.chan_type == SENSOR_CHAN_HUMIDITY);
}

static int ens210_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
			       const void *user_data, struct sensor_frame_reading *reading)
{
	const uint8_t *value;

	ARG_UNUSED(user_data);

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_AMBIENT_TEMP:
		value = &frame[ENS210_T_VAL_POS];
		break;
	case SENSOR_CHAN_HUMIDITY:
		value = &frame[ENS210_H_VAL_POS];
		break;
	default:
		return -ENOTSUP;
	}

	/* No measurement of this channel, for example when it is disabled */
	if ((value[2] & ENS210_VALUE_VALID) == 0U) {
		return 0;
	}

	if (reading == NULL) {
		return 1;
	}

	if (chan_spec.chan_type == SENSOR_CHAN_AMBIENT_TEMP) {
		reading->values[0] = sensor_raw_to_q31(sys_get_le16(value), ENS210_VALUE_BITS,
						       ENS210_TEMP_SCALE) -
				     ENS210_KELVIN_OFFSET;
	} else {
		reading->values[0] =
			sensor_raw_to_q31(sys_get_le16(value), ENS210_VALUE_BITS, ENS210_HUM_SCALE);
	}

	return 1;
}

static void ens210_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
			      struct sensor_raw_frames *frames)
{
	const struct ens210_encoded_data *edata = (const struct ens210_encoded_data *)buffer;

	*frames = (struct sensor_raw_frames){
		.frames = (const uint8_t *)&edata->temp,
		.size = ENS210_FRAME_SIZE,
		.frame_size = ENS210_FRAME_SIZE,
		.decode_frame = ens210_decode_frame,
		.timestamp_ns = edata->header.base_timestamp_ns,
		.shift = (chan_spec.chan_type == SENSOR_CHAN_AMBIENT_TEMP) ? ENS210_TEMP_SHIFT
									   : ENS210_HUMIDITY_SHIFT,
	};
}

static int ens210_decoder_get_frame_count(const uint8_t *buffer,
					  struct sensor_chan_spec chan_spec,
					  uint16_t *frame_count)
{
	struct sensor_raw_frames frames;

	if (!ens210_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	ens210_get_frames(buffer, chan_spec, &frames);

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int ens210_decoder_get_size_info(struct sensor_chan_spec chan_spec,
					size_t *base_size, size_t *frame_size)
{
	if (!ens210_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 1U, base_size, frame_size);
}

static int ens210_decoder_decode(const uint8_t *buffer,
				 struct sensor_chan_spec chan_spec,
				 uint32_t *fit, uint16_t max_count,
				 void *data_out)
{
	struct sensor_raw_frames frames;

	if (!ens210_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	ens210_get_frames(buffer, chan_spec, &frames);

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = ens210_decoder_get_frame_count,
	.get_size_info = ens210_decoder_get_size_info,
	.decode = ens210_decoder_decode,
};

int ens210_get_decoder(const struct device *dev,
			   const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();
	return 0;
}
