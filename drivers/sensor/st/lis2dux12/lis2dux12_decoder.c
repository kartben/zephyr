/* ST Microelectronics LIS2DUX12 6-axis IMU sensor driver
 *
 * Copyright (c) 2023 Google LLC
 * Copyright (c) 2024 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/dt-bindings/sensor/lis2dux12.h>
#include <zephyr/sys/byteorder.h>

#include "lis2dux12.h"
#include "lis2dux12_decoder.h"

/* FIFO sample period at the accelerometer ODR */
static const uint64_t accel_period_ns[] = {
	[LIS2DUX12_DT_ODR_OFF] = 0U,
	[LIS2DUX12_DT_ODR_1Hz_ULP] = SENSOR_ODR_MHZ_TO_PERIOD_NS(1000),
	[LIS2DUX12_DT_ODR_3Hz_ULP] = SENSOR_ODR_MHZ_TO_PERIOD_NS(3000),
	[LIS2DUX12_DT_ODR_25Hz_ULP] = SENSOR_ODR_MHZ_TO_PERIOD_NS(25000),
	[LIS2DUX12_DT_ODR_6Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(6000),
	[LIS2DUX12_DT_ODR_12Hz5] = SENSOR_ODR_MHZ_TO_PERIOD_NS(12500),
	[LIS2DUX12_DT_ODR_25Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(25000),
	[LIS2DUX12_DT_ODR_50Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(50000),
	[LIS2DUX12_DT_ODR_100Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(100000),
	[LIS2DUX12_DT_ODR_200Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(200000),
	[LIS2DUX12_DT_ODR_400Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(400000),
	[LIS2DUX12_DT_ODR_800Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(800000),
};

/* FIFO batch data rate divisor of the ODR */
static const uint8_t accel_divisor[] = {
	[LIS2DUX12_DT_BDR_XL_ODR] = 1U,
	[LIS2DUX12_DT_BDR_XL_ODR_DIV_2] = 2U,
	[LIS2DUX12_DT_BDR_XL_ODR_DIV_4] = 4U,
	[LIS2DUX12_DT_BDR_XL_ODR_DIV_8] = 8U,
	[LIS2DUX12_DT_BDR_XL_ODR_DIV_16] = 16U,
	[LIS2DUX12_DT_BDR_XL_ODR_DIV_32] = 32U,
	[LIS2DUX12_DT_BDR_XL_ODR_DIV_64] = 64U,
	[LIS2DUX12_DT_BDR_XL_ODR_OFF] = 0U,
};

/* Accelerometer shift for each full-scale range */
static const int8_t accel_shift[] = {
	[LIS2DUX12_DT_FS_2G] = 5,
	[LIS2DUX12_DT_FS_4G] = 6,
	[LIS2DUX12_DT_FS_8G] = 7,
	[LIS2DUX12_DT_FS_16G] = 8,
};

/* Scale of a 16-bit accelerometer sample: GAIN_UNIT ug/LSB at +/-2 g, doubling with the range */
#define LIS2DUX12_ACCEL_SCALE(mult, shift)                                                         \
	SENSOR_Q31_SCALE(GAIN_UNIT * (mult) * SENSOR_G, 1000000000000LL, (shift))

static const int32_t accel_scale[] = {
	[LIS2DUX12_DT_FS_2G] = LIS2DUX12_ACCEL_SCALE(1, 5),
	[LIS2DUX12_DT_FS_4G] = LIS2DUX12_ACCEL_SCALE(2, 6),
	[LIS2DUX12_DT_FS_8G] = LIS2DUX12_ACCEL_SCALE(4, 7),
	[LIS2DUX12_DT_FS_16G] = LIS2DUX12_ACCEL_SCALE(8, 8),
};

/* Temperature: 25 degrees Celsius at 0 LSB, 355.5 LSB per degree Celsius */
#define LIS2DUX12_TEMP_SHIFT      9
#define LIS2DUX12_TEMP_OFFSET_Q31 (25 * (1 << (31 - LIS2DUX12_TEMP_SHIFT)))
#define LIS2DUX12_TEMP_LSB_NUM    2
#define LIS2DUX12_TEMP_LSB_DEN    711

/* Values of the fifo_mode_sel header field */
#define LIS2DUX12_FIFO_XL12_TEMP 0U
#define LIS2DUX12_FIFO_XL16      1U
#define LIS2DUX12_FIFO_XL8_2X    2U

struct lis2dux12_frame_format {
	bool is_fifo;
	uint8_t fifo_mode_sel;
	int32_t accel_scale;
};

static int lis2dux12_num_values(struct sensor_chan_spec chan_spec)
{
	if (chan_spec.chan_idx != 0U) {
		return -ENOTSUP;
	}

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z:
	case SENSOR_CHAN_ACCEL_XYZ:
		/* All accelerometer channels are decoded as three-axis data */
		return 3;
	case SENSOR_CHAN_DIE_TEMP:
		return IS_ENABLED(CONFIG_LIS2DUX12_ENABLE_TEMP) ? 1 : -ENOTSUP;
	default:
		return -ENOTSUP;
	}
}

/*
 * Get the raw values of a single sample buffer: the three accelerometer axes, or the
 * temperature in raw[0]. Return the number of readings for the channel.
 */
static int lis2dux12_sample_raw_get(const uint8_t *frame, bool is_temp, uint16_t raw[3])
{
	const struct lis2dux12_rtio_data *edata = (const struct lis2dux12_rtio_data *)frame;

	if (is_temp) {
		if (edata->has_temp == 0U) {
			return 0;
		}

		raw[0] = sys_get_le16(edata->temp);

		return 1;
	}

	if (edata->has_accel == 0U) {
		return 0;
	}

	for (uint8_t i = 0U; i < 3U; i++) {
		raw[i] = sys_get_le16(&edata->acc[i * 2U]);
	}

	return 1;
}

/*
 * Get the raw values of reading index of a FIFO word, left-justified to 16 bits. Return the
 * number of readings of the word for the channel.
 */
static int lis2dux12_fifo_raw_get(const uint8_t *frame, uint8_t fifo_mode_sel, bool is_temp,
				  uint8_t index, uint16_t raw[3])
{
	const uint8_t *data = &frame[1];
	uint32_t lo;
	uint32_t hi;

	switch (frame[0] >> 3) {
	case LIS2DUXXX_XL_TEMP_TAG:
		if (fifo_mode_sel != LIS2DUX12_FIFO_XL12_TEMP) {
			/* 16-bit accelerometer sample */
			if (is_temp) {
				return 0;
			}

			for (uint8_t i = 0U; i < 3U; i++) {
				raw[i] = sys_get_le16(&data[i * 2U]);
			}

			return 1;
		}

		/* 12-bit accelerometer and temperature samples */
		lo = sys_get_le24(&data[0]);
		hi = sys_get_le24(&data[3]);

		if (is_temp) {
			raw[0] = (uint16_t)((hi >> 12) << 4);
		} else {
			raw[0] = (uint16_t)((lo & 0xFFFU) << 4);
			raw[1] = (uint16_t)((lo >> 12) << 4);
			raw[2] = (uint16_t)((hi & 0xFFFU) << 4);
		}

		return 1;
	case LIS2DUXXX_XL_ONLY_2X_TAG:
		/* Two 8-bit accelerometer samples, the older one first */
		if (is_temp) {
			return 0;
		}

		for (uint8_t i = 0U; i < 3U; i++) {
			raw[i] = (uint16_t)(data[index * 3U + i] << 8);
		}

		return 2;
	default:
		/* Timestamp and embedded function words */
		return 0;
	}
}

static int lis2dux12_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				  const void *user_data, struct sensor_frame_reading *reading)
{
	const struct lis2dux12_frame_format *fmt = user_data;
	const bool is_temp = chan_spec.chan_type == SENSOR_CHAN_DIE_TEMP;
	uint16_t raw[3];
	int num;

	if (fmt->is_fifo) {
		num = lis2dux12_fifo_raw_get(frame, fmt->fifo_mode_sel, is_temp,
					     (reading != NULL) ? reading->index : 0U, raw);
	} else {
		num = lis2dux12_sample_raw_get(frame, is_temp, raw);
	}

	if (num == 0 || reading == NULL) {
		return num;
	}

	if (is_temp) {
		reading->values[0] =
			LIS2DUX12_TEMP_OFFSET_Q31 +
			sensor_raw_to_q31_ratio(raw[0], 16U, LIS2DUX12_TEMP_LSB_NUM,
						LIS2DUX12_TEMP_LSB_DEN, LIS2DUX12_TEMP_SHIFT);
	} else {
		for (uint8_t i = 0U; i < 3U; i++) {
			reading->values[i] = sensor_raw_to_q31(raw[i], 16U, fmt->accel_scale);
		}
	}

	return num;
}

static int lis2dux12_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				struct sensor_raw_frames *frames,
				struct lis2dux12_frame_format *fmt)
{
	const struct lis2dux12_decoder_header *header =
		(const struct lis2dux12_decoder_header *)buffer;
	int num_values = lis2dux12_num_values(chan_spec);

	if (num_values < 0) {
		return num_values;
	}

	*fmt = (struct lis2dux12_frame_format){
		.is_fifo = IS_ENABLED(CONFIG_LIS2DUX12_STREAM) && header->is_fifo == 1U,
		.accel_scale = accel_scale[header->range],
	};

	*frames = (struct sensor_raw_frames){
		.frames = buffer,
		.size = sizeof(struct lis2dux12_rtio_data),
		.frame_size = sizeof(struct lis2dux12_rtio_data),
		.decode_frame = lis2dux12_decode_frame,
		.user_data = fmt,
		.timestamp_ns = header->timestamp,
		.shift = (chan_spec.chan_type == SENSOR_CHAN_DIE_TEMP) ? LIS2DUX12_TEMP_SHIFT
								       : accel_shift[header->range],
		.num_values = (uint8_t)num_values,
	};

	if (fmt->is_fifo) {
		const struct lis2dux12_fifo_data *edata =
			(const struct lis2dux12_fifo_data *)buffer;

		if (edata->accel_odr >= ARRAY_SIZE(accel_period_ns)) {
			return -EINVAL;
		}

		fmt->fifo_mode_sel = edata->fifo_mode_sel;
		frames->frames = buffer + sizeof(*edata);
		frames->size = LIS2DUX12_FIFO_SIZE(edata->fifo_count);
		frames->frame_size = LIS2DUX12_FIFO_ITEM_LEN;
		frames->period_ns =
			accel_period_ns[edata->accel_odr] * accel_divisor[edata->accel_batch_odr];
	}

	return 0;
}

static int lis2dux12_decoder_get_frame_count(const uint8_t *buffer,
					     struct sensor_chan_spec chan_spec,
					     uint16_t *frame_count)
{
	struct sensor_raw_frames frames;
	struct lis2dux12_frame_format fmt;
	int rc;

	rc = lis2dux12_get_frames(buffer, chan_spec, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int lis2dux12_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				    uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_raw_frames frames;
	struct lis2dux12_frame_format fmt;
	int rc;

	rc = lis2dux12_get_frames(buffer, chan_spec, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

static int lis2dux12_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					   size_t *frame_size)
{
	int num_values = lis2dux12_num_values(chan_spec);

	if (num_values < 0) {
		return num_values;
	}

	return sensor_decode_frames_size_info(chan_spec, (uint8_t)num_values, base_size,
					      frame_size);
}

static bool lis2dux12_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	const struct lis2dux12_decoder_header *header =
		(const struct lis2dux12_decoder_header *)buffer;

	if (!IS_ENABLED(CONFIG_LIS2DUX12_STREAM)) {
		return false;
	}

	switch (trigger) {
	case SENSOR_TRIG_DATA_READY:
		return (header->int_status & 0x01U) != 0U;
	case SENSOR_TRIG_FIFO_WATERMARK:
		return (header->int_status & 0x80U) != 0U;
	case SENSOR_TRIG_FIFO_FULL:
		return (header->int_status & 0x40U) != 0U;
	default:
		return false;
	}
}

#define DT_DRV_COMPAT st_lis2dux12

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = lis2dux12_decoder_get_frame_count,
	.get_size_info = lis2dux12_decoder_get_size_info,
	.decode = lis2dux12_decoder_decode,
	.has_trigger = lis2dux12_decoder_has_trigger,
};

int lis2dux12_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
