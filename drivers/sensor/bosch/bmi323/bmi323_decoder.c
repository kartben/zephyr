/*
 * Copyright (c) 2025 Alif Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT bosch_bmi323

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/sys/byteorder.h>

#include "bmi323.h"

#ifdef CONFIG_SENSOR_ASYNC_API

/* Index of the first word of each sensor in bmi323_encoded_data.data */
#define BMI323_WORD_ACCEL 0U
#define BMI323_WORD_GYRO  3U
#define BMI323_WORD_TEMP  6U

/* Value of a data register that holds no valid sample */
#define BMI323_INVALID_SAMPLE 0x8000U

/*
 * One degree is SENSOR_PI / 180 micro-radians. Numerator and denominator are divided by 8 so
 * that the numerator times a full scale of up to 2000 dps fits in 32 bits.
 */
#define BMI323_URAD_PER_DEG_NUM (SENSOR_PI / 8)
#define BMI323_URAD_PER_DEG_DEN (180 * 1000000 / 8)

BUILD_ASSERT((SENSOR_PI % 8) == 0);

enum bmi323_sensor {
	BMI323_ACCEL,
	BMI323_GYRO,
	BMI323_TEMP,
};

struct bmi323_chan_info {
	enum bmi323_sensor sensor;
	/* Index of the first word of the channel */
	uint8_t word;
	/* Number of values per reading */
	uint8_t num;
	int8_t shift;
};

static int bmi323_chan_info_get(struct sensor_chan_spec chan_spec, struct bmi323_chan_info *info)
{
	if (chan_spec.chan_idx != 0U) {
		return -ENOTSUP;
	}

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_ACCEL_XYZ:
		*info = (struct bmi323_chan_info){BMI323_ACCEL, BMI323_WORD_ACCEL, 3U,
						  BMI323_ACCEL_SHIFT};
		return 0;
	case SENSOR_CHAN_ACCEL_X:
		*info = (struct bmi323_chan_info){BMI323_ACCEL, BMI323_WORD_ACCEL, 1U,
						  BMI323_ACCEL_SHIFT};
		return 0;
	case SENSOR_CHAN_ACCEL_Y:
		*info = (struct bmi323_chan_info){BMI323_ACCEL, BMI323_WORD_ACCEL + 1U, 1U,
						  BMI323_ACCEL_SHIFT};
		return 0;
	case SENSOR_CHAN_ACCEL_Z:
		*info = (struct bmi323_chan_info){BMI323_ACCEL, BMI323_WORD_ACCEL + 2U, 1U,
						  BMI323_ACCEL_SHIFT};
		return 0;
	case SENSOR_CHAN_GYRO_XYZ:
		*info = (struct bmi323_chan_info){BMI323_GYRO, BMI323_WORD_GYRO, 3U,
						  BMI323_GYRO_SHIFT};
		return 0;
	case SENSOR_CHAN_GYRO_X:
		*info = (struct bmi323_chan_info){BMI323_GYRO, BMI323_WORD_GYRO, 1U,
						  BMI323_GYRO_SHIFT};
		return 0;
	case SENSOR_CHAN_GYRO_Y:
		*info = (struct bmi323_chan_info){BMI323_GYRO, BMI323_WORD_GYRO + 1U, 1U,
						  BMI323_GYRO_SHIFT};
		return 0;
	case SENSOR_CHAN_GYRO_Z:
		*info = (struct bmi323_chan_info){BMI323_GYRO, BMI323_WORD_GYRO + 2U, 1U,
						  BMI323_GYRO_SHIFT};
		return 0;
	case SENSOR_CHAN_DIE_TEMP:
		*info = (struct bmi323_chan_info){BMI323_TEMP, BMI323_WORD_TEMP, 1U,
						  BMI323_TEMP_SHIFT};
		return 0;
	default:
		return -ENOTSUP;
	}
}

/*
 * A sample is a signed fraction of the full scale: raw / 2^15 * fs_num / fs_den. Raising the
 * shift by 15 makes sensor_raw_to_q31_ratio() divide by 2^15 without rounding.
 */
static q31_t bmi323_full_scale_to_q31(uint16_t raw, int32_t fs_num, int32_t fs_den, int8_t shift)
{
	return sensor_raw_to_q31_ratio(raw, 16U, fs_num, fs_den, shift + 15);
}

static q31_t bmi323_sample_to_q31(const struct bmi323_encoded_data *edata,
				  enum bmi323_sensor sensor, uint16_t raw, int8_t shift)
{
	switch (sensor) {
	case BMI323_ACCEL:
		/* Full scale of accel_range g, in m/s^2 */
		return bmi323_full_scale_to_q31(raw, (int32_t)(edata->accel_range * SENSOR_G),
						1000000, shift);
	case BMI323_GYRO:
		/* Full scale of gyro_range dps, in rad/s */
		return bmi323_full_scale_to_q31(
			raw, (int32_t)(edata->gyro_range * BMI323_URAD_PER_DEG_NUM),
			BMI323_URAD_PER_DEG_DEN, shift);
	default: {
		/* Temperature in micro-degrees Celsius, then in degrees Celsius */
		int32_t micro_deg =
			(int32_t)(sign_extend(raw, 15U) * IMU_BOSCH_DIE_TEMP_MICRO_DEG_CELSIUS_LSB +
				  IMU_BOSCH_DIE_TEMP_OFFSET_MICRO_DEG_CELSIUS);

		return sensor_raw_to_q31_ratio((uint32_t)micro_deg, 32U, 1, 1000000, shift);
	}
	}
}

static int bmi323_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
			       const void *user_data, struct sensor_frame_reading *reading)
{
	const struct bmi323_encoded_data *edata = user_data;
	struct bmi323_chan_info info;
	bool present;
	int rc;

	rc = bmi323_chan_info_get(chan_spec, &info);
	if (rc != 0) {
		return rc;
	}

	switch (info.sensor) {
	case BMI323_ACCEL:
		present = edata->has_accel;
		break;
	case BMI323_GYRO:
		present = edata->has_gyro;
		break;
	default:
		present = edata->has_temp;
		break;
	}

	if (!present) {
		return 0;
	}

	if (reading == NULL) {
		return 1;
	}

	for (uint8_t i = 0U; i < info.num; i++) {
		uint16_t raw = sys_get_le16(&frame[(info.word + i) * sizeof(uint16_t)]);

		if (raw == BMI323_INVALID_SAMPLE) {
			return -ENODATA;
		}

		reading->values[i] = bmi323_sample_to_q31(edata, info.sensor, raw, info.shift);
	}

	return 1;
}

static int bmi323_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
			     struct sensor_raw_frames *frames)
{
	const struct bmi323_encoded_data *edata = (const struct bmi323_encoded_data *)buffer;
	struct bmi323_chan_info info;
	int rc;

	rc = bmi323_chan_info_get(chan_spec, &info);
	if (rc != 0) {
		return rc;
	}

	*frames = (struct sensor_raw_frames){
		.frames = edata->data,
		.size = sizeof(edata->data),
		.frame_size = sizeof(edata->data),
		.decode_frame = bmi323_decode_frame,
		.user_data = edata,
		.timestamp_ns = edata->header.timestamp,
		.shift = info.shift,
		.num_values = info.num,
	};

	return 0;
}

static int bmi323_decoder_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
					  uint16_t *frame_count)
{
	struct sensor_raw_frames frames;
	int rc;

	rc = bmi323_get_frames(buffer, chan_spec, &frames);
	if (rc != 0) {
		return rc;
	}

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int bmi323_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					size_t *frame_size)
{
	struct bmi323_chan_info info;
	int rc;

	rc = bmi323_chan_info_get(chan_spec, &info);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames_size_info(chan_spec, info.num, base_size, frame_size);
}

static int bmi323_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				 uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_raw_frames frames;
	int rc;

	rc = bmi323_get_frames(buffer, chan_spec, &frames);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
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
