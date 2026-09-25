/* ST Microelectronics LSM6DSVXXX family IMU sensor
 *
 * Copyright (c) 2025 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Datasheet:
 * https://www.st.com/resource/en/datasheet/lsm6dsv320x.pdf
 * https://www.st.com/resource/en/datasheet/lsm6dsv80x.pdf
 */

#include <math.h>

#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/dt-bindings/sensor/lsm6dsvxxx.h>
#include <zephyr/sys/byteorder.h>

#include "lsm6dsvxxx.h"

/* Gyroscope full scale: 125 dps (0) to 4000 dps (5), 4.375 mdps/LSB at 125 dps */
#define LSM6DSVXXX_GYRO_FS_MAX    5U
#define LSM6DSVXXX_GYRO_SHIFT(fs) ((int8_t)(2 + (fs)))

/*
 * The sensitivity doubles at each full scale step while the shift grows by one, so the scale
 * factor is the one of the 125 dps full scale, 4375 udps/LSB.
 */
#define LSM6DSVXXX_GYRO_Q31_SCALE                                                                  \
	SENSOR_Q31_SCALE(4375LL * SENSOR_PI, 180LL * 1000000LL * 1000000LL,                        \
			 LSM6DSVXXX_GYRO_SHIFT(0))

/* Temperature: 355 LSB/C, 0 LSB at 25 C */
#define LSM6DSVXXX_TEMP_SHIFT      9
#define LSM6DSVXXX_TEMP_LSB_PER_C  355
#define LSM6DSVXXX_TEMP_OFFSET_LSB (25 * LSM6DSVXXX_TEMP_LSB_PER_C)

/* SFLP gravity vector in mg */
#define LSM6DSVXXX_GRAVITY_SHIFT 12

/* SFLP game rotation vector: unit quaternion */
#define LSM6DSVXXX_GAME_ROT_SHIFT 0

/* Source of the data of a channel */
enum lsm6dsvxxx_source {
	LSM6DSVXXX_SRC_NONE,
	LSM6DSVXXX_SRC_ACCEL,
	LSM6DSVXXX_SRC_GYRO,
	LSM6DSVXXX_SRC_TEMP,
	LSM6DSVXXX_SRC_GAME_ROT,
	LSM6DSVXXX_SRC_GRAVITY,
	LSM6DSVXXX_SRC_GBIAS,
};

struct lsm6dsvxxx_frame_format {
	const struct lsm6dsvxxx_config *cfg;
	enum lsm6dsvxxx_source src;
	/* Accelerometer sensitivity in micro-m/s^2 per LSB */
	int32_t accel_scale;
	int8_t shift;
	bool is_fifo;
};

#ifdef CONFIG_LSM6DSVXXX_STREAM
/* Accelerometer and gyroscope batch rates share the same encoding */
BUILD_ASSERT(LSM6DSVXXX_DT_XL_BATCHED_AT_1Hz875 == LSM6DSVXXX_DT_GY_BATCHED_AT_1Hz875 &&
	     LSM6DSVXXX_DT_XL_BATCHED_AT_7680Hz == LSM6DSVXXX_DT_GY_BATCHED_AT_7680Hz);

static const uint64_t xl_gy_period_ns[] = {
	[LSM6DSVXXX_DT_XL_NOT_BATCHED] = 0U,
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

static const uint64_t temp_period_ns[] = {
	[LSM6DSVXXX_DT_TEMP_NOT_BATCHED] = 0U,
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

static enum lsm6dsvxxx_source lsm6dsvxxx_tag_source(uint8_t tag)
{
	switch (tag) {
	case LSM6DSVXXX_XL_NC_TAG:
	case LSM6DSVXXX_XL_HG_TAG:
		return LSM6DSVXXX_SRC_ACCEL;
	case LSM6DSVXXX_GY_NC_TAG:
		return LSM6DSVXXX_SRC_GYRO;
	case LSM6DSVXXX_TEMPERATURE_TAG:
		return LSM6DSVXXX_SRC_TEMP;
	case LSM6DSVXXX_SFLP_GAME_ROTATION_VECTOR_TAG:
		return LSM6DSVXXX_SRC_GAME_ROT;
	case LSM6DSVXXX_SFLP_GRAVITY_VECTOR_TAG:
		return LSM6DSVXXX_SRC_GRAVITY;
	case LSM6DSVXXX_SFLP_GYROSCOPE_BIAS_TAG:
		return LSM6DSVXXX_SRC_GBIAS;
	default:
		return LSM6DSVXXX_SRC_NONE;
	}
}

static q31_t lsm6dsvxxx_float_to_q31(float32_t val, int8_t shift)
{
	int64_t q = (int64_t)roundf(val * (float32_t)BIT64(31 - shift));

	return (q31_t)CLAMP(q, INT32_MIN, INT32_MAX);
}

static float32_t calculate_quat_w(float32_t *x, float32_t *y, float32_t *z)
{
	float32_t sumsq = (*x * *x) + (*y * *y) + (*z * *z);

	/*
	 * Theoretically sumsq should never be greater than 1, but due to
	 * lack of precision it might happen. So, add a software correction
	 * which consists in normalizing the (x, y, z) vector.
	 */
	if (sumsq > 1.0f) {
		float32_t n = sqrtf(sumsq);

		*x /= n;
		*y /= n;
		*z /= n;
		sumsq = 1.0f;
	}

	/* unity vector quaternions */
	return sqrtf(1.0f - sumsq);
}

static void lsm6dsvxxx_decode_game_rot(const struct lsm6dsvxxx_frame_format *fmt,
				       const uint8_t *data, q31_t *values)
{
	const struct lsm6dsvxxx_chip_api *api = fmt->cfg->chip_api;
	union {
		float32_t f;
		uint32_t i;
	} x, y, z;
	float32_t w;

	x.i = api->from_f16_to_f32(sys_get_le16(&data[0]));
	y.i = api->from_f16_to_f32(sys_get_le16(&data[2]));
	z.i = api->from_f16_to_f32(sys_get_le16(&data[4]));

	w = calculate_quat_w(&x.f, &y.f, &z.f);

	values[0] = lsm6dsvxxx_float_to_q31(x.f, fmt->shift);
	values[1] = lsm6dsvxxx_float_to_q31(y.f, fmt->shift);
	values[2] = lsm6dsvxxx_float_to_q31(z.f, fmt->shift);
	values[3] = lsm6dsvxxx_float_to_q31(w, fmt->shift);
}

static void lsm6dsvxxx_decode_gravity(const struct lsm6dsvxxx_frame_format *fmt,
				      const uint8_t *data, q31_t *values)
{
	const struct lsm6dsvxxx_chip_api *api = fmt->cfg->chip_api;

	for (uint8_t i = 0U; i < 3U; i++) {
		float32_t mg = api->from_sflp_to_mg((int16_t)sys_get_le16(&data[i * 2U]));

		values[i] = lsm6dsvxxx_float_to_q31(mg, fmt->shift);
	}
}

static int lsm6dsvxxx_fifo_period(const struct lsm6dsvxxx_fifo_data *fifo,
				  enum lsm6dsvxxx_source src, uint64_t *period_ns)
{
	const uint64_t *table;
	size_t size;
	uint8_t idx;

	switch (src) {
	case LSM6DSVXXX_SRC_ACCEL:
		table = xl_gy_period_ns;
		size = ARRAY_SIZE(xl_gy_period_ns);
		idx = fifo->accel_batch_odr;
		break;
	case LSM6DSVXXX_SRC_GYRO:
		table = xl_gy_period_ns;
		size = ARRAY_SIZE(xl_gy_period_ns);
		idx = fifo->gyro_batch_odr;
		break;
	case LSM6DSVXXX_SRC_TEMP:
		table = temp_period_ns;
		size = ARRAY_SIZE(temp_period_ns);
		idx = fifo->temp_batch_odr;
		break;
	default:
		table = sflp_period_ns;
		size = ARRAY_SIZE(sflp_period_ns);
		idx = fifo->sflp_batch_odr;
		break;
	}

	if (idx >= size) {
		return -EINVAL;
	}

	*period_ns = table[idx];

	return 0;
}
#endif /* CONFIG_LSM6DSVXXX_STREAM */

static enum lsm6dsvxxx_source lsm6dsvxxx_chan_source(struct sensor_chan_spec chan_spec)
{
	if (chan_spec.chan_idx != 0U) {
		return LSM6DSVXXX_SRC_NONE;
	}

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z:
	case SENSOR_CHAN_ACCEL_XYZ:
		return LSM6DSVXXX_SRC_ACCEL;
	case SENSOR_CHAN_GYRO_X:
	case SENSOR_CHAN_GYRO_Y:
	case SENSOR_CHAN_GYRO_Z:
	case SENSOR_CHAN_GYRO_XYZ:
		return LSM6DSVXXX_SRC_GYRO;
	case SENSOR_CHAN_DIE_TEMP:
		return IS_ENABLED(CONFIG_LSM6DSVXXX_ENABLE_TEMP) ? LSM6DSVXXX_SRC_TEMP
								 : LSM6DSVXXX_SRC_NONE;
	case SENSOR_CHAN_GAME_ROTATION_VECTOR:
		return IS_ENABLED(CONFIG_LSM6DSVXXX_STREAM) ? LSM6DSVXXX_SRC_GAME_ROT
							    : LSM6DSVXXX_SRC_NONE;
	case SENSOR_CHAN_GRAVITY_VECTOR:
		return IS_ENABLED(CONFIG_LSM6DSVXXX_STREAM) ? LSM6DSVXXX_SRC_GRAVITY
							    : LSM6DSVXXX_SRC_NONE;
	case SENSOR_CHAN_GBIAS_XYZ:
		return IS_ENABLED(CONFIG_LSM6DSVXXX_STREAM) ? LSM6DSVXXX_SRC_GBIAS
							    : LSM6DSVXXX_SRC_NONE;
	default:
		return LSM6DSVXXX_SRC_NONE;
	}
}

/* Get the raw data of the source in a frame, NULL when the frame does not carry it */
static const uint8_t *lsm6dsvxxx_frame_data(const uint8_t *frame,
					    const struct lsm6dsvxxx_frame_format *fmt)
{
	const struct lsm6dsvxxx_rtio_data *sample = (const struct lsm6dsvxxx_rtio_data *)frame;

#ifdef CONFIG_LSM6DSVXXX_STREAM
	if (fmt->is_fifo) {
		/* FIFO word: TAG byte, then X, Y and Z little-endian */
		return (lsm6dsvxxx_tag_source(frame[0] >> 3) == fmt->src) ? &frame[1] : NULL;
	}
#endif /* CONFIG_LSM6DSVXXX_STREAM */

	switch (fmt->src) {
	case LSM6DSVXXX_SRC_ACCEL:
		return (sample->has_accel != 0U) ? (const uint8_t *)sample->accel : NULL;
	case LSM6DSVXXX_SRC_GYRO:
		return (sample->has_gyro != 0U) ? (const uint8_t *)sample->gyro : NULL;
	case LSM6DSVXXX_SRC_TEMP:
		return (sample->has_temp != 0U) ? (const uint8_t *)&sample->temp : NULL;
	default:
		return NULL;
	}
}

static q31_t lsm6dsvxxx_convert(const struct lsm6dsvxxx_frame_format *fmt, uint16_t raw)
{
	switch (fmt->src) {
	case LSM6DSVXXX_SRC_ACCEL:
		return sensor_raw_to_q31_ratio(raw, 16U, fmt->accel_scale, 1000000, fmt->shift);
	case LSM6DSVXXX_SRC_TEMP:
		return sensor_raw_to_q31_ratio(
			(uint32_t)((int32_t)(int16_t)raw + LSM6DSVXXX_TEMP_OFFSET_LSB), 32U, 1,
			LSM6DSVXXX_TEMP_LSB_PER_C, fmt->shift);
	default:
		/* Gyroscope and gyroscope bias */
		return sensor_raw_to_q31(raw, 16U, LSM6DSVXXX_GYRO_Q31_SCALE);
	}
}

static int lsm6dsvxxx_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				   const void *user_data, struct sensor_frame_reading *reading)
{
	const struct lsm6dsvxxx_frame_format *fmt = user_data;
	const uint8_t *data = lsm6dsvxxx_frame_data(frame, fmt);
	uint8_t first = 0U;
	uint8_t num = 1U;

	if (data == NULL) {
		return 0;
	}

	if (reading == NULL) {
		return 1;
	}

#ifdef CONFIG_LSM6DSVXXX_STREAM
	if (fmt->src == LSM6DSVXXX_SRC_GAME_ROT) {
		lsm6dsvxxx_decode_game_rot(fmt, data, reading->values);
		return 1;
	}

	if (fmt->src == LSM6DSVXXX_SRC_GRAVITY) {
		lsm6dsvxxx_decode_gravity(fmt, data, reading->values);
		return 1;
	}
#endif /* CONFIG_LSM6DSVXXX_STREAM */

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_ACCEL_XYZ:
	case SENSOR_CHAN_GYRO_XYZ:
	case SENSOR_CHAN_GBIAS_XYZ:
		num = 3U;
		break;
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_GYRO_Y:
		first = 1U;
		break;
	case SENSOR_CHAN_ACCEL_Z:
	case SENSOR_CHAN_GYRO_Z:
		first = 2U;
		break;
	default:
		/* X axis or temperature */
		break;
	}

	for (uint8_t i = 0U; i < num; i++) {
		reading->values[i] = lsm6dsvxxx_convert(fmt, sys_get_le16(&data[(first + i) * 2U]));
	}

	return 1;
}

static int lsm6dsvxxx_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				 struct sensor_raw_frames *frames,
				 struct lsm6dsvxxx_frame_format *fmt)
{
	const struct lsm6dsvxxx_decoder_header *header =
		(const struct lsm6dsvxxx_decoder_header *)buffer;
	const struct lsm6dsvxxx_config *cfg = header->cfg;

	*fmt = (struct lsm6dsvxxx_frame_format){
		.cfg = cfg,
		.src = lsm6dsvxxx_chan_source(chan_spec),
	};

	switch (fmt->src) {
	case LSM6DSVXXX_SRC_ACCEL:
		fmt->accel_scale = cfg->accel_scaler[header->accel_fs];
		fmt->shift = cfg->accel_bit_shift[header->accel_fs];
		break;
	case LSM6DSVXXX_SRC_GYRO:
		if (header->gyro_fs > LSM6DSVXXX_GYRO_FS_MAX) {
			return -EINVAL;
		}
		fmt->shift = LSM6DSVXXX_GYRO_SHIFT(header->gyro_fs);
		break;
	case LSM6DSVXXX_SRC_GBIAS:
		/* Gyroscope bias is reported at 125 dps full scale */
		fmt->shift = LSM6DSVXXX_GYRO_SHIFT(0);
		break;
	case LSM6DSVXXX_SRC_TEMP:
		fmt->shift = LSM6DSVXXX_TEMP_SHIFT;
		break;
	case LSM6DSVXXX_SRC_GRAVITY:
		fmt->shift = LSM6DSVXXX_GRAVITY_SHIFT;
		break;
	case LSM6DSVXXX_SRC_GAME_ROT:
		fmt->shift = LSM6DSVXXX_GAME_ROT_SHIFT;
		break;
	default:
		return -ENOTSUP;
	}

	*frames = (struct sensor_raw_frames){
		.frames = buffer,
		.size = sizeof(struct lsm6dsvxxx_rtio_data),
		.frame_size = sizeof(struct lsm6dsvxxx_rtio_data),
		.decode_frame = lsm6dsvxxx_decode_frame,
		.user_data = fmt,
		.timestamp_ns = header->timestamp,
		.shift = fmt->shift,
	};

#ifdef CONFIG_LSM6DSVXXX_STREAM
	if (header->is_fifo != 0U) {
		const struct lsm6dsvxxx_fifo_data *fifo =
			(const struct lsm6dsvxxx_fifo_data *)buffer;

		/* The header timestamp is the one of the newest FIFO word of each source */
		fmt->is_fifo = true;
		frames->frames = buffer + sizeof(*fifo);
		frames->size = (size_t)fifo->fifo_count * LSM6DSVXXX_FIFO_ITEM_LEN;
		frames->frame_size = LSM6DSVXXX_FIFO_ITEM_LEN;

		return lsm6dsvxxx_fifo_period(fifo, fmt->src, &frames->period_ns);
	}
#endif /* CONFIG_LSM6DSVXXX_STREAM */

	/* A one-shot sample holds no sensor fusion output */
	switch (fmt->src) {
	case LSM6DSVXXX_SRC_ACCEL:
	case LSM6DSVXXX_SRC_GYRO:
	case LSM6DSVXXX_SRC_TEMP:
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int lsm6dsvxxx_decoder_get_frame_count(const uint8_t *buffer,
					      struct sensor_chan_spec chan_spec,
					      uint16_t *frame_count)
{
	struct sensor_raw_frames frames;
	struct lsm6dsvxxx_frame_format fmt;
	int rc;

	rc = lsm6dsvxxx_get_frames(buffer, chan_spec, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int lsm6dsvxxx_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				     uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_raw_frames frames;
	struct lsm6dsvxxx_frame_format fmt;
	int rc;

	rc = lsm6dsvxxx_get_frames(buffer, chan_spec, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

static int lsm6dsvxxx_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					    size_t *frame_size)
{
	if (lsm6dsvxxx_chan_source(chan_spec) == LSM6DSVXXX_SRC_NONE) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 0U, base_size, frame_size);
}

static bool lsm6dsvxxx_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	ARG_UNUSED(buffer);
	ARG_UNUSED(trigger);

	return false;
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = lsm6dsvxxx_decoder_get_frame_count,
	.get_size_info = lsm6dsvxxx_decoder_get_size_info,
	.decode = lsm6dsvxxx_decoder_decode,
	.has_trigger = lsm6dsvxxx_decoder_has_trigger,
};

int lsm6dsvxxx_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
