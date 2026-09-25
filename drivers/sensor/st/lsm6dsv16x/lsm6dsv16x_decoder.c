/* ST Microelectronics LSM6DSV16X 6-axis IMU sensor driver
 *
 * Copyright (c) 2023 Google LLC
 * Copyright (c) 2024 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/dt-bindings/sensor/lsm6dsv16x.h>
#include <zephyr/sys/byteorder.h>

#include "lsm6dsv16x.h"
#include "lsm6dsv16x_decoder.h"

/* Denominators of the LSB values below, which are in nano-units or micro-units */
#define LSM6DSV16X_NANO  1000000000
#define LSM6DSV16X_MICRO 1000000

/* Accelerometer LSB at +/-2 g, and SFLP gravity vector LSB: 61 ug, in nm/s^2 */
#define LSM6DSV16X_XL_LSB_NMS2 ((GAIN_UNIT_XL * SENSOR_G + 500LL) / 1000LL)

/* Gyroscope LSB at +/-125 dps: 4.375 mdps, in nrad/s */
#define LSM6DSV16X_GY_LSB_NRADS ((GAIN_UNIT_G * SENSOR_PI + 90000LL) / 180000LL)

/* Temperature: 256 LSB/degC, 0 LSB at 25 degC */
#define LSM6DSV16X_TEMP_SHIFT  9
#define LSM6DSV16X_TEMP_SCALE  SENSOR_Q31_SCALE(1, 256, LSM6DSV16X_TEMP_SHIFT)
#define LSM6DSV16X_TEMP_OFFSET SENSOR_Q31_SCALE(25, 1, LSM6DSV16X_TEMP_SHIFT)

/* SFLP gyroscope bias: +/-125 dps range */
#define LSM6DSV16X_GBIAS_SHIFT 2

/* SFLP gravity vector: +/-2 g range */
#define LSM6DSV16X_GRAVITY_SHIFT 5

/* Sensor hub magnetometer: 1500 uGauss/LSB */
#define LSM6DSV16X_MAGN_SHIFT      9
#define LSM6DSV16X_MAGN_LSB_UGAUSS 1500

/* Sensor hub barometer: 4096 LSB/hPa, that is 40960 LSB/kPa */
#define LSM6DSV16X_PRESS_SHIFT       8
#define LSM6DSV16X_PRESS_LSB_PER_KPA 40960

/* Assumed period of the sensor hub data, which the buffer does not record */
#define LSM6DSV16X_SHUB_PERIOD_NS SENSOR_ODR_MHZ_TO_PERIOD_NS(10000)

static const uint64_t accel_period_ns[] = {
	[LSM6DSVXXX_DT_XL_BATCHED_AT_1Hz875] = SENSOR_ODR_MHZ_TO_PERIOD_NS(1875),
	[LSM6DSVXXX_DT_XL_BATCHED_AT_7Hz5] = SENSOR_ODR_MHZ_TO_PERIOD_NS(7500),
	[LSM6DSVXXX_DT_XL_BATCHED_AT_15Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(15000),
	[LSM6DSVXXX_DT_XL_BATCHED_AT_30Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(30000),
	[LSM6DSVXXX_DT_XL_BATCHED_AT_60Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(60000),
	[LSM6DSVXXX_DT_XL_BATCHED_AT_120Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(120000),
	[LSM6DSVXXX_DT_XL_BATCHED_AT_240Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(240000),
	[LSM6DSVXXX_DT_XL_BATCHED_AT_480Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(480000),
	[LSM6DSVXXX_DT_XL_BATCHED_AT_960Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(960000),
	[LSM6DSVXXX_DT_XL_BATCHED_AT_1920Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(1920000),
	[LSM6DSVXXX_DT_XL_BATCHED_AT_3840Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(3840000),
	[LSM6DSVXXX_DT_XL_BATCHED_AT_7680Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(7680000),
};

static const uint64_t gyro_period_ns[] = {
	[LSM6DSVXXX_DT_GY_BATCHED_AT_1Hz875] = SENSOR_ODR_MHZ_TO_PERIOD_NS(1875),
	[LSM6DSVXXX_DT_GY_BATCHED_AT_7Hz5] = SENSOR_ODR_MHZ_TO_PERIOD_NS(7500),
	[LSM6DSVXXX_DT_GY_BATCHED_AT_15Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(15000),
	[LSM6DSVXXX_DT_GY_BATCHED_AT_30Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(30000),
	[LSM6DSVXXX_DT_GY_BATCHED_AT_60Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(60000),
	[LSM6DSVXXX_DT_GY_BATCHED_AT_120Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(120000),
	[LSM6DSVXXX_DT_GY_BATCHED_AT_240Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(240000),
	[LSM6DSVXXX_DT_GY_BATCHED_AT_480Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(480000),
	[LSM6DSVXXX_DT_GY_BATCHED_AT_960Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(960000),
	[LSM6DSVXXX_DT_GY_BATCHED_AT_1920Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(1920000),
	[LSM6DSVXXX_DT_GY_BATCHED_AT_3840Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(3840000),
	[LSM6DSVXXX_DT_GY_BATCHED_AT_7680Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(7680000),
};

static const uint64_t temp_period_ns[] = {
	[LSM6DSVXXX_DT_TEMP_BATCHED_AT_1Hz875] = SENSOR_ODR_MHZ_TO_PERIOD_NS(1875),
	[LSM6DSVXXX_DT_TEMP_BATCHED_AT_15Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(15000),
	[LSM6DSVXXX_DT_TEMP_BATCHED_AT_60Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(60000),
};

static const uint64_t sflp_period_ns[] = {
	[LSM6DSVXXX_DT_SFLP_ODR_AT_15Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(15000),
	[LSM6DSVXXX_DT_SFLP_ODR_AT_30Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(30000),
	[LSM6DSVXXX_DT_SFLP_ODR_AT_60Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(60000),
	[LSM6DSVXXX_DT_SFLP_ODR_AT_120Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(120000),
	[LSM6DSVXXX_DT_SFLP_ODR_AT_240Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(240000),
	[LSM6DSVXXX_DT_SFLP_ODR_AT_480Hz] = SENSOR_ODR_MHZ_TO_PERIOD_NS(480000),
};

/* Shift of the accelerometer data for a given accel_fs_idx value (+/-2 g to +/-32 g) */
static const int8_t accel_range[] = {5, 6, 7, 8, 9};

/* Shift of the gyroscope data and log2 of its LSB in 4.375 mdps units, for a given fs */
static const struct {
	int8_t shift;
	uint8_t lsb_log2;
} gyro_range[] = {
	[LSM6DSV16X_DT_FS_125DPS] = {2, 0},  [LSM6DSV16X_DT_FS_250DPS] = {3, 1},
	[LSM6DSV16X_DT_FS_500DPS] = {4, 2},  [LSM6DSV16X_DT_FS_1000DPS] = {5, 3},
	[LSM6DSV16X_DT_FS_2000DPS] = {6, 4}, [LSM6DSV16X_DT_FS_4000DPS] = {7, 5},
};

/* Conversion of the raw data of the decoded channel */
struct lsm6dsv16x_frame_format {
	/* Header of a FIFO buffer, NULL for a single sample */
	const struct lsm6dsv16x_fifo_data *fifo;
	/* Value of one LSB: num / den of the channel unit */
	int32_t num;
	int32_t den;
	int8_t shift;
};

/*
 * Get the channel holding all axes of chan and the index of the axis for a single axis channel
 */
static enum sensor_channel lsm6dsv16x_chan_group(enum sensor_channel chan, uint8_t *axis)
{
	*axis = 0U;

	switch (chan) {
	case SENSOR_CHAN_ACCEL_Z:
		(*axis)++;
		__fallthrough;
	case SENSOR_CHAN_ACCEL_Y:
		(*axis)++;
		__fallthrough;
	case SENSOR_CHAN_ACCEL_X:
		return SENSOR_CHAN_ACCEL_XYZ;
	case SENSOR_CHAN_GYRO_Z:
		(*axis)++;
		__fallthrough;
	case SENSOR_CHAN_GYRO_Y:
		(*axis)++;
		__fallthrough;
	case SENSOR_CHAN_GYRO_X:
		return SENSOR_CHAN_GYRO_XYZ;
	default:
		return chan;
	}
}

static bool lsm6dsv16x_chan_is_supported(struct sensor_chan_spec chan_spec, bool is_fifo)
{
	uint8_t axis;

	if (chan_spec.chan_idx != 0U) {
		return false;
	}

	switch (lsm6dsv16x_chan_group(chan_spec.chan_type, &axis)) {
	case SENSOR_CHAN_ACCEL_XYZ:
	case SENSOR_CHAN_GYRO_XYZ:
	case SENSOR_CHAN_DIE_TEMP:
		return true;
	case SENSOR_CHAN_GAME_ROTATION_VECTOR:
	case SENSOR_CHAN_GRAVITY_VECTOR:
	case SENSOR_CHAN_GBIAS_XYZ:
		return is_fifo;
	case SENSOR_CHAN_MAGN_XYZ:
	case SENSOR_CHAN_PRESS:
		return is_fifo && IS_ENABLED(CONFIG_LSM6DSV16X_SENSORHUB);
	default:
		return false;
	}
}

/* Get the channel of the data held by a FIFO word, SENSOR_CHAN_ALL for other words */
static enum sensor_channel lsm6dsv16x_fifo_word_chan(const struct lsm6dsv16x_fifo_data *fifo,
						     const uint8_t *word)
{
	const uint8_t tag = word[0] >> 3;

#if !defined(CONFIG_LSM6DSV16X_SENSORHUB)
	/* Only used to look up the sensor hub targets */
	ARG_UNUSED(fifo);
#endif

	switch (tag) {
	case LSM6DSV16X_XL_NC_TAG:
		return SENSOR_CHAN_ACCEL_XYZ;
	case LSM6DSV16X_GY_NC_TAG:
		return SENSOR_CHAN_GYRO_XYZ;
	case LSM6DSV16X_TEMPERATURE_TAG:
		return SENSOR_CHAN_DIE_TEMP;
	case LSM6DSV16X_SFLP_GAME_ROTATION_VECTOR_TAG:
		return SENSOR_CHAN_GAME_ROTATION_VECTOR;
	case LSM6DSV16X_SFLP_GYROSCOPE_BIAS_TAG:
		return SENSOR_CHAN_GBIAS_XYZ;
	case LSM6DSV16X_SFLP_GRAVITY_VECTOR_TAG:
		return SENSOR_CHAN_GRAVITY_VECTOR;
#if defined(CONFIG_LSM6DSV16X_SENSORHUB)
	case LSM6DSV16X_SENSORHUB_SLAVE1_TAG:
	case LSM6DSV16X_SENSORHUB_SLAVE2_TAG:
	case LSM6DSV16X_SENSORHUB_SLAVE3_TAG: {
		/* External device n is read by sensor hub target n + 1 */
		const uint8_t k = tag - LSM6DSV16X_SENSORHUB_SLAVE1_TAG;

		if (k < fifo->num_ext_dev) {
			return lsm6dsv16x_shub_type(fifo->shub_ext[k]);
		}
		break;
	}
#endif /* CONFIG_LSM6DSV16X_SENSORHUB */
	default:
		break;
	}

	return SENSOR_CHAN_ALL;
}

/*
 * Get the raw data of a group channel from a frame, returning the number of readings it holds
 */
static int lsm6dsv16x_raw_get(const uint8_t *frame, const struct lsm6dsv16x_frame_format *fmt,
			      enum sensor_channel group, uint32_t raw[3])
{
	const struct lsm6dsv16x_rtio_data *sample = (const struct lsm6dsv16x_rtio_data *)frame;
	const int16_t *values;
	bool present;

	if (fmt->fifo != NULL) {
		if (lsm6dsv16x_fifo_word_chan(fmt->fifo, frame) != group) {
			return 0;
		}

		if (group == SENSOR_CHAN_PRESS) {
			raw[0] = sys_get_le24(&frame[1]);
		} else {
			for (uint8_t i = 0U; i < 3U; i++) {
				raw[i] = sys_get_le16(&frame[1U + i * 2U]);
			}
		}

		return 1;
	}

	/* Single sample, with the data in CPU byte order */
	switch (group) {
	case SENSOR_CHAN_ACCEL_XYZ:
		present = sample->has_accel == 1U;
		values = sample->acc;
		break;
	case SENSOR_CHAN_GYRO_XYZ:
		present = sample->has_gyro == 1U;
		values = sample->gyro;
		break;
	case SENSOR_CHAN_DIE_TEMP:
		present = sample->has_temp == 1U;
		values = &sample->temp;
		break;
	default:
		return 0;
	}

	if (!present) {
		return 0;
	}

	for (uint8_t i = 0U; i < ((group == SENSOR_CHAN_DIE_TEMP) ? 1U : 3U); i++) {
		raw[i] = (uint16_t)values[i];
	}

	return 1;
}

static q31_t lsm6dsv16x_unit_to_q31(float32_t value)
{
	const int64_t q = (int64_t)roundf(value * 2147483648.0f);

	return (q31_t)CLAMP(q, INT32_MIN, INT32_MAX);
}

/* Convert the three half-precision float components of a unit quaternion to x, y, z, w */
static void lsm6dsv16x_game_rotation_get(const uint32_t raw[3], q31_t *values)
{
	union {
		float32_t f;
		uint32_t i;
	} v[3];
	float32_t sumsq = 0.0f;

	for (uint8_t i = 0U; i < 3U; i++) {
		v[i].i = lsm6dsv16x_from_f16_to_f32((uint16_t)raw[i]);
		sumsq += v[i].f * v[i].f;
	}

	/*
	 * Theoretically sumsq should never be greater than 1, but due to lack of precision it
	 * might happen. So, add a software correction which consists in normalizing the (x, y, z)
	 * vector.
	 */
	if (sumsq > 1.0f) {
		const float32_t n = sqrtf(sumsq);

		for (uint8_t i = 0U; i < 3U; i++) {
			v[i].f /= n;
		}
		sumsq = 1.0f;
	}

	for (uint8_t i = 0U; i < 3U; i++) {
		values[i] = lsm6dsv16x_unit_to_q31(v[i].f);
	}

	values[3] = lsm6dsv16x_unit_to_q31(sqrtf(1.0f - sumsq));
}

static int lsm6dsv16x_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				   const void *user_data, struct sensor_frame_reading *reading)
{
	const struct lsm6dsv16x_frame_format *fmt = user_data;
	uint32_t raw[3];
	uint8_t axis;
	enum sensor_channel group = lsm6dsv16x_chan_group(chan_spec.chan_type, &axis);
	int rc;

	rc = lsm6dsv16x_raw_get(frame, fmt, group, raw);
	if (rc <= 0 || reading == NULL) {
		return rc;
	}

	switch (group) {
	case SENSOR_CHAN_DIE_TEMP:
		reading->values[0] = sensor_raw_to_q31(raw[0], 16U, LSM6DSV16X_TEMP_SCALE) +
				     LSM6DSV16X_TEMP_OFFSET;
		break;
	case SENSOR_CHAN_GAME_ROTATION_VECTOR:
		lsm6dsv16x_game_rotation_get(raw, reading->values);
		break;
	case SENSOR_CHAN_PRESS:
		reading->values[0] =
			sensor_raw_to_q31_ratio(raw[0], 24U, fmt->num, fmt->den, fmt->shift);
		break;
	default:
		if (group != chan_spec.chan_type) {
			/* Single axis */
			reading->values[0] = sensor_raw_to_q31_ratio(raw[axis], 16U, fmt->num,
								     fmt->den, fmt->shift);
			break;
		}

		for (uint8_t i = 0U; i < 3U; i++) {
			reading->values[i] = sensor_raw_to_q31_ratio(raw[i], 16U, fmt->num,
								     fmt->den, fmt->shift);
		}
		break;
	}

	return 1;
}

/*
 * Get the period of a batch rate code. Codes past the end of the table are invalid. Code 0 of
 * the accelerometer, gyroscope and temperature tables means not batched and gives a period of
 * 0: the FIFO then holds no word of that sensor.
 */
static int lsm6dsv16x_period_get(const uint64_t *table, size_t size, uint8_t odr,
				 uint64_t *period_ns)
{
	if (odr >= size) {
		return -EINVAL;
	}

	*period_ns = table[odr];

	return 0;
}

static int lsm6dsv16x_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				 struct sensor_raw_frames *frames,
				 struct lsm6dsv16x_frame_format *fmt)
{
	const struct lsm6dsv16x_decoder_header *header =
		(const struct lsm6dsv16x_decoder_header *)buffer;
	const struct lsm6dsv16x_fifo_data *fifo = (const struct lsm6dsv16x_fifo_data *)buffer;
	const bool is_fifo = IS_ENABLED(CONFIG_LSM6DSV16X_STREAM) && header->is_fifo == 1U;
	uint64_t period_ns = 0U;
	uint8_t axis;
	int rc = 0;

	if (!lsm6dsv16x_chan_is_supported(chan_spec, is_fifo)) {
		return -ENOTSUP;
	}

	*fmt = (struct lsm6dsv16x_frame_format){
		.fifo = is_fifo ? fifo : NULL,
		.den = LSM6DSV16X_NANO,
	};

	switch (lsm6dsv16x_chan_group(chan_spec.chan_type, &axis)) {
	case SENSOR_CHAN_ACCEL_XYZ:
		if (header->accel_fs_idx >= ARRAY_SIZE(accel_range)) {
			return -EINVAL;
		}
		fmt->shift = accel_range[header->accel_fs_idx];
		fmt->num = (int32_t)(LSM6DSV16X_XL_LSB_NMS2 << header->accel_fs_idx);
		if (is_fifo) {
			rc = lsm6dsv16x_period_get(accel_period_ns, ARRAY_SIZE(accel_period_ns),
						   fifo->accel_batch_odr, &period_ns);
		}
		break;
	case SENSOR_CHAN_GYRO_XYZ:
		if (header->gyro_fs >= ARRAY_SIZE(gyro_range) ||
		    gyro_range[header->gyro_fs].shift == 0) {
			return -EINVAL;
		}
		fmt->shift = gyro_range[header->gyro_fs].shift;
		fmt->num =
			(int32_t)(LSM6DSV16X_GY_LSB_NRADS << gyro_range[header->gyro_fs].lsb_log2);
		if (is_fifo) {
			rc = lsm6dsv16x_period_get(gyro_period_ns, ARRAY_SIZE(gyro_period_ns),
						   fifo->gyro_batch_odr, &period_ns);
		}
		break;
	case SENSOR_CHAN_DIE_TEMP:
		fmt->shift = LSM6DSV16X_TEMP_SHIFT;
		if (is_fifo) {
			rc = lsm6dsv16x_period_get(temp_period_ns, ARRAY_SIZE(temp_period_ns),
						   fifo->temp_batch_odr, &period_ns);
		}
		break;
	case SENSOR_CHAN_GAME_ROTATION_VECTOR:
		/* Quaternion components are in [-1, 1] */
		fmt->shift = 0;
		rc = lsm6dsv16x_period_get(sflp_period_ns, ARRAY_SIZE(sflp_period_ns),
					   fifo->sflp_batch_odr, &period_ns);
		break;
	case SENSOR_CHAN_GBIAS_XYZ:
		fmt->shift = LSM6DSV16X_GBIAS_SHIFT;
		fmt->num = (int32_t)LSM6DSV16X_GY_LSB_NRADS;
		rc = lsm6dsv16x_period_get(sflp_period_ns, ARRAY_SIZE(sflp_period_ns),
					   fifo->sflp_batch_odr, &period_ns);
		break;
	case SENSOR_CHAN_GRAVITY_VECTOR:
		fmt->shift = LSM6DSV16X_GRAVITY_SHIFT;
		fmt->num = (int32_t)LSM6DSV16X_XL_LSB_NMS2;
		rc = lsm6dsv16x_period_get(sflp_period_ns, ARRAY_SIZE(sflp_period_ns),
					   fifo->sflp_batch_odr, &period_ns);
		break;
	case SENSOR_CHAN_MAGN_XYZ:
		fmt->shift = LSM6DSV16X_MAGN_SHIFT;
		fmt->num = LSM6DSV16X_MAGN_LSB_UGAUSS;
		fmt->den = LSM6DSV16X_MICRO;
		period_ns = LSM6DSV16X_SHUB_PERIOD_NS;
		break;
	case SENSOR_CHAN_PRESS:
		fmt->shift = LSM6DSV16X_PRESS_SHIFT;
		fmt->num = 1;
		fmt->den = LSM6DSV16X_PRESS_LSB_PER_KPA;
		period_ns = LSM6DSV16X_SHUB_PERIOD_NS;
		break;
	default:
		return -ENOTSUP;
	}

	if (rc != 0) {
		return rc;
	}

	*frames = (struct sensor_raw_frames){
		.decode_frame = lsm6dsv16x_decode_frame,
		.user_data = fmt,
		.timestamp_ns = header->timestamp,
		.period_ns = period_ns,
		.shift = fmt->shift,
	};

	if (is_fifo) {
		frames->frames = buffer + sizeof(*fifo);
		frames->size = (size_t)fifo->fifo_count * LSM6DSV16X_FIFO_ITEM_LEN;
		frames->frame_size = LSM6DSV16X_FIFO_ITEM_LEN;
	} else {
		frames->frames = buffer;
		frames->size = sizeof(struct lsm6dsv16x_rtio_data);
		frames->frame_size = sizeof(struct lsm6dsv16x_rtio_data);
	}

	return 0;
}

static int lsm6dsv16x_decoder_get_frame_count(const uint8_t *buffer,
					      struct sensor_chan_spec chan_spec,
					      uint16_t *frame_count)
{
	struct sensor_raw_frames frames;
	struct lsm6dsv16x_frame_format fmt;
	int rc;

	rc = lsm6dsv16x_get_frames(buffer, chan_spec, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int lsm6dsv16x_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				     uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_raw_frames frames;
	struct lsm6dsv16x_frame_format fmt;
	int rc;

	rc = lsm6dsv16x_get_frames(buffer, chan_spec, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

static int lsm6dsv16x_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					    size_t *frame_size)
{
	if (!lsm6dsv16x_chan_is_supported(chan_spec, IS_ENABLED(CONFIG_LSM6DSV16X_STREAM))) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 0U, base_size, frame_size);
}

static bool lsm6dsv16x_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	return false;
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = lsm6dsv16x_decoder_get_frame_count,
	.get_size_info = lsm6dsv16x_decoder_get_size_info,
	.decode = lsm6dsv16x_decoder_decode,
	.has_trigger = lsm6dsv16x_decoder_has_trigger,
};

int lsm6dsv16x_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
