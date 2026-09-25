/*
 * Copyright (c) 2026 Analog Devices Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT adi_adis1647x

#include <stddef.h>

#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/sys/byteorder.h>

#include "adis1647x.h"

/* Shifts covering the full 16-bit range of every model */
#define ADIS1647X_ACCEL_SHIFT 9  /* +/-40.96 g = +/-401.7 m/s^2 */
#define ADIS1647X_GYRO_SHIFT  6  /* +/-3276.8 dps = +/-57.2 rad/s */
#define ADIS1647X_TEMP_SHIFT  12 /* +/-3276.8 degC */

/*
 * One accel LSB is accel_scale_num / 100 mg, that is accel_scale_num * SENSOR_G /
 * (100000 * 1000000) m/s^2. Both terms are divided by 50 to fit in 32 bits.
 */
BUILD_ASSERT((SENSOR_G % 50) == 0);
#define ADIS1647X_ACCEL_LSB_NUM ((int32_t)(SENSOR_G / 50))
#define ADIS1647X_ACCEL_LSB_DEN ((int32_t)(100000LL * 1000000LL / 50))

/*
 * One gyro LSB is gyro_scale_num / 100000 dps, that is gyro_scale_num * pi / (180 * 100000)
 * rad/s, with pi approximated by 355 / 113 (relative error 8.5e-8).
 */
#define ADIS1647X_GYRO_LSB_NUM 355
#define ADIS1647X_GYRO_LSB_DEN ((int32_t)(113LL * 180 * 100000))

/* One temperature LSB is 0.1 degC */
#define ADIS1647X_TEMP_LSB_NUM 1
#define ADIS1647X_TEMP_LSB_DEN 10

/* Conversion of the requested channel */
struct adis1647x_chan_format {
	/* Offset of the first value in struct adis1647x_burst_data */
	size_t offset;
	/* Number of consecutive values */
	uint8_t num_values;
	/* Value of one LSB in the base unit of the channel: num / den */
	int32_t num;
	int32_t den;
	int8_t shift;
};

static int adis1647x_chan_format_get(const struct adis1647x_sample_data *sample,
				     struct sensor_chan_spec chan_spec,
				     struct adis1647x_chan_format *fmt)
{
	if (chan_spec.chan_idx != 0U) {
		return -ENOTSUP;
	}

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_XYZ:
		fmt->offset = offsetof(struct adis1647x_burst_data, x_accel_out);
		break;
	case SENSOR_CHAN_ACCEL_Y:
		fmt->offset = offsetof(struct adis1647x_burst_data, y_accel_out);
		break;
	case SENSOR_CHAN_ACCEL_Z:
		fmt->offset = offsetof(struct adis1647x_burst_data, z_accel_out);
		break;
	case SENSOR_CHAN_GYRO_X:
	case SENSOR_CHAN_GYRO_XYZ:
		fmt->offset = offsetof(struct adis1647x_burst_data, x_gyro_out);
		break;
	case SENSOR_CHAN_GYRO_Y:
		fmt->offset = offsetof(struct adis1647x_burst_data, y_gyro_out);
		break;
	case SENSOR_CHAN_GYRO_Z:
		fmt->offset = offsetof(struct adis1647x_burst_data, z_gyro_out);
		break;
	case SENSOR_CHAN_DIE_TEMP:
		fmt->offset = offsetof(struct adis1647x_burst_data, temp_out);
		break;
	default:
		return -ENOTSUP;
	}

	fmt->num_values = SENSOR_CHANNEL_3_AXIS(chan_spec.chan_type) ? 3U : 1U;

	if (sample == NULL) {
		return 0;
	}

	if (SENSOR_CHANNEL_IS_ACCEL(chan_spec.chan_type)) {
		fmt->num = (int32_t)sample->accel_scale_num * ADIS1647X_ACCEL_LSB_NUM;
		fmt->den = ADIS1647X_ACCEL_LSB_DEN;
		fmt->shift = ADIS1647X_ACCEL_SHIFT;
	} else if (SENSOR_CHANNEL_IS_GYRO(chan_spec.chan_type)) {
		fmt->num = (int32_t)sample->gyro_scale_num * ADIS1647X_GYRO_LSB_NUM;
		fmt->den = ADIS1647X_GYRO_LSB_DEN;
		fmt->shift = ADIS1647X_GYRO_SHIFT;
	} else {
		fmt->num = ADIS1647X_TEMP_LSB_NUM;
		fmt->den = ADIS1647X_TEMP_LSB_DEN;
		fmt->shift = ADIS1647X_TEMP_SHIFT;
	}

	return 0;
}

static int adis1647x_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				  const void *user_data, struct sensor_frame_reading *reading)
{
	const struct adis1647x_chan_format *fmt = user_data;

	ARG_UNUSED(chan_spec);

	if (reading == NULL) {
		return 1;
	}

	for (uint8_t i = 0U; i < fmt->num_values; i++) {
		reading->values[i] =
			sensor_raw_to_q31_ratio(sys_get_be16(&frame[fmt->offset + i * 2U]), 16U,
						fmt->num, fmt->den, fmt->shift);
	}

	return 1;
}

/* Describe the single burst frame of a buffer for the requested channel */
static int adis1647x_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				struct sensor_raw_frames *frames, struct adis1647x_chan_format *fmt)
{
	const struct adis1647x_sample_data *sample = (const struct adis1647x_sample_data *)buffer;
	int rc;

	rc = adis1647x_chan_format_get(sample, chan_spec, fmt);
	if (rc != 0) {
		return rc;
	}

	*frames = (struct sensor_raw_frames){
		.frames = buffer + offsetof(struct adis1647x_sample_data, burst_data),
		.size = sizeof(struct adis1647x_burst_data),
		.frame_size = sizeof(struct adis1647x_burst_data),
		.decode_frame = adis1647x_decode_frame,
		.user_data = fmt,
		.timestamp_ns = sample->timestamp,
		.shift = fmt->shift,
	};

	return 0;
}

static int adis1647x_decoder_get_frame_count(const uint8_t *buffer,
					     struct sensor_chan_spec chan_spec,
					     uint16_t *frame_count)
{
	struct sensor_raw_frames frames;
	struct adis1647x_chan_format fmt;
	int rc;

	rc = adis1647x_get_frames(buffer, chan_spec, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int adis1647x_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				    uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_raw_frames frames;
	struct adis1647x_chan_format fmt;
	int rc;

	rc = adis1647x_get_frames(buffer, chan_spec, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

static bool adis1647x_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	return trigger == SENSOR_TRIG_DATA_READY;
}

static int adis1647x_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					   size_t *frame_size)
{
	struct adis1647x_chan_format fmt;
	int rc;

	rc = adis1647x_chan_format_get(NULL, chan_spec, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames_size_info(chan_spec, fmt.num_values, base_size, frame_size);
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = adis1647x_decoder_get_frame_count,
	.decode = adis1647x_decoder_decode,
	.has_trigger = adis1647x_decoder_has_trigger,
	.get_size_info = adis1647x_decoder_get_size_info,
};

int adis1647x_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
