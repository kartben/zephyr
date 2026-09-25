/*
 * Copyright (c) 2025 Alif Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT bosch_bmi323

#include <limits.h>

#include <zephyr/drivers/sensor.h>

#include "bmi323.h"

#ifdef CONFIG_SENSOR_ASYNC_API

/* A data register holds 0x8000 when no valid sample is available */
static bool bmi323_sample_is_valid(int16_t raw)
{
	return raw != INT16_MIN;
}

/* Consume the frame without a reading, as it holds no valid sample for the channel */
static int bmi323_drop_reading(uint32_t *fit)
{
	*fit = 1U;
	return 0;
}

/* Q31 conversion for accel:
 * q31 = raw * range_g * SENSOR_G / 1000000 * 2^21 / 32768
 *     = raw * range_g * SENSOR_G / 1000000 * 64
 *     = raw * range_g * SENSOR_G / 15625
 */
static int32_t bmi323_accel_to_q31(int16_t raw, uint32_t range_g)
{
	return (int32_t)(((int64_t)raw * range_g * SENSOR_G) / 15625LL);
}

/* Q31 conversion for gyro (degrees to radians):
 * q31 = raw * range_dps * π / 180 * 2^21 / 32768
 *     = raw * range_dps * SENSOR_PI / 180 / 1000000 * 64
 *     = raw * range_dps * SENSOR_PI / 2812500
 */
static int32_t bmi323_gyro_to_q31(int16_t raw, uint32_t range_dps)
{
	return (int32_t)(((int64_t)raw * range_dps * SENSOR_PI) / 2812500LL);
}

/* Convert raw to micro-degrees C,
 * then to Q31 with shift=10:
 * q31 = micro_deg * 2^21 / 1000000
 */
static int32_t bmi323_temp_to_q31(int16_t raw_temp)
{
	int64_t micro_deg = (int64_t)raw_temp * IMU_BOSCH_DIE_TEMP_MICRO_DEG_CELSIUS_LSB +
			    IMU_BOSCH_DIE_TEMP_OFFSET_MICRO_DEG_CELSIUS;

	return (int32_t)((micro_deg * (1LL << (31 - BMI323_TEMP_SHIFT))) / 1000000LL);
}

static int bmi323_decoder_get_frame_count(const uint8_t *buffer,
						struct sensor_chan_spec chan,
						uint16_t *frame_count)
{
	const struct bmi323_encoded_data *edata =
		(const struct bmi323_encoded_data *)buffer;

	if (chan.chan_idx != 0) {
		return -ENOTSUP;
	}

	switch (chan.chan_type) {
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z:
	case SENSOR_CHAN_ACCEL_XYZ:
		*frame_count = edata->has_accel ? 1U : 0U;
		return 0;
	case SENSOR_CHAN_GYRO_X:
	case SENSOR_CHAN_GYRO_Y:
	case SENSOR_CHAN_GYRO_Z:
	case SENSOR_CHAN_GYRO_XYZ:
		*frame_count = edata->has_gyro ? 1U : 0U;
		return 0;
	case SENSOR_CHAN_DIE_TEMP:
		*frame_count = edata->has_temp ? 1U : 0U;
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int bmi323_decoder_get_size_info(struct sensor_chan_spec chan,
					size_t *base_size,
					size_t *frame_size)
{
	switch (chan.chan_type) {
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z:
	case SENSOR_CHAN_GYRO_X:
	case SENSOR_CHAN_GYRO_Y:
	case SENSOR_CHAN_GYRO_Z:
	case SENSOR_CHAN_DIE_TEMP:
		*base_size = sizeof(struct sensor_q31_data);
		*frame_size = sizeof(struct sensor_q31_sample_data);
		return 0;
	case SENSOR_CHAN_ACCEL_XYZ:
	case SENSOR_CHAN_GYRO_XYZ:
		*base_size = sizeof(struct sensor_three_axis_data);
		*frame_size = sizeof(struct sensor_three_axis_sample_data);
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int bmi323_decoder_decode(const uint8_t *buffer,
				 struct sensor_chan_spec chan,
				 uint32_t *fit,
				 uint16_t max_count,
				 void *data_out)
{
	const struct bmi323_encoded_data *edata =
		(const struct bmi323_encoded_data *)buffer;

	if (*fit != 0U) {
		return 0;
	}

	if (max_count == 0U || chan.chan_idx != 0U) {
		return -EINVAL;
	}

	switch (chan.chan_type) {
	case SENSOR_CHAN_ACCEL_XYZ: {
		struct sensor_three_axis_data *out = data_out;

		if (!edata->has_accel) {
			return -ENODATA;
		}

		if (!bmi323_sample_is_valid(edata->reading.accel_x) ||
		    !bmi323_sample_is_valid(edata->reading.accel_y) ||
		    !bmi323_sample_is_valid(edata->reading.accel_z)) {
			return bmi323_drop_reading(fit);
		}

		out->header.base_timestamp_ns    = edata->header.timestamp;
		out->header.reading_count        = 1U;
		out->shift                       = BMI323_ACCEL_SHIFT;
		out->readings[0].timestamp_delta = 0U;
		out->readings[0].x =
			bmi323_accel_to_q31(edata->reading.accel_x,
				    edata->accel_range);
		out->readings[0].y =
			bmi323_accel_to_q31(edata->reading.accel_y,
				    edata->accel_range);
		out->readings[0].z =
			bmi323_accel_to_q31(edata->reading.accel_z,
				    edata->accel_range);
		break;
	}

	case SENSOR_CHAN_GYRO_XYZ: {
		struct sensor_three_axis_data *out = data_out;

		if (!edata->has_gyro) {
			return -ENODATA;
		}

		if (!bmi323_sample_is_valid(edata->reading.gyro_x) ||
		    !bmi323_sample_is_valid(edata->reading.gyro_y) ||
		    !bmi323_sample_is_valid(edata->reading.gyro_z)) {
			return bmi323_drop_reading(fit);
		}

		out->header.base_timestamp_ns    = edata->header.timestamp;
		out->header.reading_count        = 1U;
		out->shift                       = BMI323_GYRO_SHIFT;
		out->readings[0].timestamp_delta = 0U;
		out->readings[0].x = bmi323_gyro_to_q31(edata->reading.gyro_x, edata->gyro_range);
		out->readings[0].y = bmi323_gyro_to_q31(edata->reading.gyro_y, edata->gyro_range);
		out->readings[0].z = bmi323_gyro_to_q31(edata->reading.gyro_z, edata->gyro_range);
		break;
	}

	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z: {
		struct sensor_q31_data *out = data_out;
		int16_t raw;

		if (!edata->has_accel) {
			return -ENODATA;
		}

		if (chan.chan_type == SENSOR_CHAN_ACCEL_X) {
			raw = edata->reading.accel_x;
		} else if (chan.chan_type == SENSOR_CHAN_ACCEL_Y) {
			raw = edata->reading.accel_y;
		} else {
			raw = edata->reading.accel_z;
		}

		if (!bmi323_sample_is_valid(raw)) {
			return bmi323_drop_reading(fit);
		}

		out->header.base_timestamp_ns       = edata->header.timestamp;
		out->header.reading_count           = 1U;
		out->readings[0].timestamp_delta    = 0U;
		out->shift                          = BMI323_ACCEL_SHIFT;
		out->readings[0].value = bmi323_accel_to_q31(raw, edata->accel_range);
		break;
	}

	case SENSOR_CHAN_GYRO_X:
	case SENSOR_CHAN_GYRO_Y:
	case SENSOR_CHAN_GYRO_Z: {
		struct sensor_q31_data *out = data_out;
		int16_t raw;

		if (!edata->has_gyro) {
			return -ENODATA;
		}

		if (chan.chan_type == SENSOR_CHAN_GYRO_X) {
			raw = edata->reading.gyro_x;
		} else if (chan.chan_type == SENSOR_CHAN_GYRO_Y) {
			raw = edata->reading.gyro_y;
		} else {
			raw = edata->reading.gyro_z;
		}

		if (!bmi323_sample_is_valid(raw)) {
			return bmi323_drop_reading(fit);
		}

		out->header.base_timestamp_ns    = edata->header.timestamp;
		out->header.reading_count        = 1U;
		out->shift                       = BMI323_GYRO_SHIFT;
		out->readings[0].timestamp_delta = 0U;
		out->readings[0].value = bmi323_gyro_to_q31(raw, edata->gyro_range);
		break;
	}

	case SENSOR_CHAN_DIE_TEMP: {
		struct sensor_q31_data *out = data_out;

		if (!edata->has_temp) {
			return -ENODATA;
		}

		if (!bmi323_sample_is_valid(edata->reading.temperature)) {
			return bmi323_drop_reading(fit);
		}

		out->header.base_timestamp_ns    = edata->header.timestamp;
		out->header.reading_count        = 1U;
		out->shift                       = BMI323_TEMP_SHIFT;
		out->readings[0].timestamp_delta = 0U;
		out->readings[0].value =
			bmi323_temp_to_q31(edata->reading.temperature);
		break;
	}

	default:
		return -ENOTSUP;
	}

	*fit = 1U;
	return 1;
}

static bool bmi323_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	ARG_UNUSED(buffer);
	ARG_UNUSED(trigger);

	return false;
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = bmi323_decoder_get_frame_count,
	.get_size_info = bmi323_decoder_get_size_info,
	.decode = bmi323_decoder_decode,
	.has_trigger = bmi323_decoder_has_trigger,
};

int bmi323_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();
	return 0;
}

#endif /* CONFIG_SENSOR_ASYNC_API */
