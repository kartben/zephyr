/*
 * Copyright (c) 2026 Analog Devices Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/byteorder.h>
#include "adis1647x.h"
/* 1000 converts from mg to g, 100 rescales accel_scale */
#define ADIS1647X_ACCEL_SCALE_DEN (1000LL * 100)
/* 100000 rescales gyro_scale, 180 for PI/180 rad->deg conversion */
#define ADIS1647X_GYRO_SCALE_DEN  (100000LL * 180)

/*
 * q31 shifts sized for the widest full scale across the supported models:
 * +/-402 m/s^2, +/-58 rad/s and +/-3277 degrees Celsius.
 */
#define ADIS1647X_ACCEL_SHIFT 9
#define ADIS1647X_GYRO_SHIFT  6
#define ADIS1647X_TEMP_SHIFT  12

void adis1647x_accel_convert(struct sensor_value *val, int16_t raw, uint8_t accel_scale_num)
{
	int64_t micro_m_s2 =
		((int64_t)raw * SENSOR_G * accel_scale_num) / ADIS1647X_ACCEL_SCALE_DEN;

	val->val1 = (int32_t)(micro_m_s2 / 1000000);
	val->val2 = (int32_t)(micro_m_s2 % 1000000);
}

void adis1647x_gyro_convert(struct sensor_value *val, int16_t raw, uint16_t gyro_scale_num)
{
	int64_t micro_rad_s =
		((int64_t)raw * SENSOR_PI * gyro_scale_num) / ADIS1647X_GYRO_SCALE_DEN;

	val->val1 = (int32_t)(micro_rad_s / 1000000);
	val->val2 = (int32_t)(micro_rad_s % 1000000);
}

static q31_t adis1647x_accel_to_q31(const struct adis1647x_sample_data *data, uint16_t raw_be)
{
	int16_t raw = (int16_t)sys_be16_to_cpu(raw_be);
	int64_t micro_m_s2 =
		((int64_t)raw * SENSOR_G * data->accel_scale_num) / ADIS1647X_ACCEL_SCALE_DEN;

	return (q31_t)((micro_m_s2 * (INT64_C(1) << (31 - ADIS1647X_ACCEL_SHIFT))) / 1000000);
}

static q31_t adis1647x_gyro_to_q31(const struct adis1647x_sample_data *data, uint16_t raw_be)
{
	int16_t raw = (int16_t)sys_be16_to_cpu(raw_be);
	int64_t micro_rad_s =
		((int64_t)raw * SENSOR_PI * data->gyro_scale_num) / ADIS1647X_GYRO_SCALE_DEN;

	return (q31_t)((micro_rad_s * (INT64_C(1) << (31 - ADIS1647X_GYRO_SHIFT))) / 1000000);
}

static q31_t adis1647x_temp_to_q31(uint16_t raw_be)
{
	int64_t micro_celsius = (int64_t)(int16_t)sys_be16_to_cpu(raw_be) * 100000;

	return (q31_t)((micro_celsius * (INT64_C(1) << (31 - ADIS1647X_TEMP_SHIFT))) / 1000000);
}

static void adis1647x_fill_q31(struct sensor_q31_data *out, uint64_t timestamp, int8_t shift,
			       q31_t value)
{
	out->header.base_timestamp_ns = timestamp;
	out->header.reading_count = 1;
	out->shift = shift;
	out->readings[0].timestamp_delta = 0;
	out->readings[0].value = value;
}

static void adis1647x_fill_three_axis(struct sensor_three_axis_data *out, uint64_t timestamp,
				      int8_t shift, q31_t x, q31_t y, q31_t z)
{
	out->header.base_timestamp_ns = timestamp;
	out->header.reading_count = 1;
	out->shift = shift;
	out->readings[0].timestamp_delta = 0;
	out->readings[0].x = x;
	out->readings[0].y = y;
	out->readings[0].z = z;
}

static int adis1647x_decoder_get_frame_count(const uint8_t *buffer,
					     struct sensor_chan_spec chan_spec,
					     uint16_t *frame_count)
{
	int32_t ret = -ENOTSUP;

	if (chan_spec.chan_idx != 0) {
		return ret;
	}

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z:
	case SENSOR_CHAN_ACCEL_XYZ:
	case SENSOR_CHAN_GYRO_X:
	case SENSOR_CHAN_GYRO_Y:
	case SENSOR_CHAN_GYRO_Z:
	case SENSOR_CHAN_GYRO_XYZ:
	case SENSOR_CHAN_DIE_TEMP:
		*frame_count = 1;
		ret = 0;
		break;

	default:
		break;
	}

	return ret;
}

static int adis1647x_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					   size_t *frame_size)
{
	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_ACCEL_XYZ:
	case SENSOR_CHAN_GYRO_XYZ:
		*base_size = sizeof(struct sensor_three_axis_data);
		*frame_size = sizeof(struct sensor_three_axis_sample_data);
		return 0;
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
	default:
		return -ENOTSUP;
	}
}

static int adis1647x_decode_sample(const struct adis1647x_sample_data *data,
				   struct sensor_chan_spec chan_spec, uint32_t *fit,
				   uint16_t max_count, void *data_out)
{
	const struct adis1647x_burst_data *burst_data = &data->burst_data;
	struct sensor_q31_data *q31_out = (struct sensor_q31_data *)data_out;
	struct sensor_three_axis_data *xyz_out = (struct sensor_three_axis_data *)data_out;

	if (*fit > 0 || max_count == 0) {
		return 0;
	}

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_ACCEL_X:
		adis1647x_fill_q31(q31_out, data->timestamp, ADIS1647X_ACCEL_SHIFT,
				   adis1647x_accel_to_q31(data, burst_data->x_accel_out));
		break;
	case SENSOR_CHAN_ACCEL_Y:
		adis1647x_fill_q31(q31_out, data->timestamp, ADIS1647X_ACCEL_SHIFT,
				   adis1647x_accel_to_q31(data, burst_data->y_accel_out));
		break;
	case SENSOR_CHAN_ACCEL_Z:
		adis1647x_fill_q31(q31_out, data->timestamp, ADIS1647X_ACCEL_SHIFT,
				   adis1647x_accel_to_q31(data, burst_data->z_accel_out));
		break;
	case SENSOR_CHAN_ACCEL_XYZ:
		adis1647x_fill_three_axis(xyz_out, data->timestamp, ADIS1647X_ACCEL_SHIFT,
					  adis1647x_accel_to_q31(data, burst_data->x_accel_out),
					  adis1647x_accel_to_q31(data, burst_data->y_accel_out),
					  adis1647x_accel_to_q31(data, burst_data->z_accel_out));
		break;
	case SENSOR_CHAN_GYRO_X:
		adis1647x_fill_q31(q31_out, data->timestamp, ADIS1647X_GYRO_SHIFT,
				   adis1647x_gyro_to_q31(data, burst_data->x_gyro_out));
		break;
	case SENSOR_CHAN_GYRO_Y:
		adis1647x_fill_q31(q31_out, data->timestamp, ADIS1647X_GYRO_SHIFT,
				   adis1647x_gyro_to_q31(data, burst_data->y_gyro_out));
		break;
	case SENSOR_CHAN_GYRO_Z:
		adis1647x_fill_q31(q31_out, data->timestamp, ADIS1647X_GYRO_SHIFT,
				   adis1647x_gyro_to_q31(data, burst_data->z_gyro_out));
		break;
	case SENSOR_CHAN_GYRO_XYZ:
		adis1647x_fill_three_axis(xyz_out, data->timestamp, ADIS1647X_GYRO_SHIFT,
					  adis1647x_gyro_to_q31(data, burst_data->x_gyro_out),
					  adis1647x_gyro_to_q31(data, burst_data->y_gyro_out),
					  adis1647x_gyro_to_q31(data, burst_data->z_gyro_out));
		break;
	case SENSOR_CHAN_DIE_TEMP:
		adis1647x_fill_q31(q31_out, data->timestamp, ADIS1647X_TEMP_SHIFT,
				   adis1647x_temp_to_q31(burst_data->temp_out));
		break;
	default:
		return -ENOTSUP;
	}

	*fit = 1;

	return 1;
}

static int adis1647x_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				    uint32_t *fit, uint16_t max_count, void *data_out)
{
	const struct adis1647x_sample_data *sample_data =
		(const struct adis1647x_sample_data *)buffer;

	return adis1647x_decode_sample(sample_data, chan_spec, fit, max_count, data_out);
}

static bool adis1647x_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	return trigger == SENSOR_TRIG_DATA_READY;
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = adis1647x_decoder_get_frame_count,
	.get_size_info = adis1647x_decoder_get_size_info,
	.decode = adis1647x_decoder_decode,
	.has_trigger = adis1647x_decoder_has_trigger,
};

int adis1647x_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
