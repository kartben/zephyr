/*
 * Copyright (c) 2024 Analog Devices Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/sys/byteorder.h>

#include "adxl372.h"

/* 12-bit samples of 100 mg/LSB, read with a shift of 11 (up to 2048 m/s^2) */
#define ADXL372_SAMPLE_BITS 12U
#define ADXL372_SHIFT       11
#define ADXL372_QSCALE      SENSOR_Q31_SCALE(SENSOR_G, 10 * 1000000LL, ADXL372_SHIFT)

/* Bytes per axis in a frame: 12-bit sample left-justified in a big-endian 16-bit word */
#define ADXL372_AXIS_SIZE 2U
#define ADXL372_NUM_AXES  3U

static const uint64_t accel_period_ns[] = {
	[ADXL372_ODR_400HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(400000),
	[ADXL372_ODR_800HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(800000),
	[ADXL372_ODR_1600HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(1600000),
	[ADXL372_ODR_3200HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(3200000),
	[ADXL372_ODR_6400HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(6400000),
};

struct adxl372_frame_format {
	/* Axes present in each frame, BIT(0) for X to BIT(2) for Z, packed in that order */
	uint8_t axes;
	/* Single sample converted to a frame of the FIFO format */
	uint8_t sample[ADXL372_NUM_AXES * ADXL372_AXIS_SIZE];
};

static bool adxl372_chan_is_supported(struct sensor_chan_spec chan_spec)
{
	return chan_spec.chan_idx == 0U && SENSOR_CHANNEL_IS_ACCEL(chan_spec.chan_type);
}

/* Size of the samples of the axes set in axes */
static size_t adxl372_axes_size(uint8_t axes)
{
	size_t size = 0U;

	for (uint8_t axis = 0U; axis < ADXL372_NUM_AXES; axis++) {
		if ((axes & BIT(axis)) != 0U) {
			size += ADXL372_AXIS_SIZE;
		}
	}

	return size;
}

static q31_t adxl372_axis_get(const uint8_t *frame, uint8_t axes, uint8_t axis)
{
	/* The axes present in the frame are packed in X, Y, Z order */
	const size_t offset = adxl372_axes_size(axes & (BIT(axis) - 1U));

	return sensor_raw_to_q31((uint32_t)sys_get_be16(&frame[offset]) >> 4U, ADXL372_SAMPLE_BITS,
				 ADXL372_QSCALE);
}

static int adxl372_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				const void *user_data, struct sensor_frame_reading *reading)
{
	const struct adxl372_frame_format *fmt = user_data;
	uint8_t first;
	uint8_t num;

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_ACCEL_XYZ:
		first = 0U;
		num = ADXL372_NUM_AXES;
		break;
	case SENSOR_CHAN_ACCEL_X:
		first = 0U;
		num = 1U;
		break;
	case SENSOR_CHAN_ACCEL_Y:
		first = 1U;
		num = 1U;
		break;
	case SENSOR_CHAN_ACCEL_Z:
		first = 2U;
		num = 1U;
		break;
	default:
		return -ENOTSUP;
	}

	/* Axes missing from the frames read as 0, unless all requested axes are missing */
	if ((fmt->axes & GENMASK(first + num - 1U, first)) == 0U) {
		return -ENOTSUP;
	}

	if (reading == NULL) {
		return 1;
	}

	for (uint8_t i = 0U; i < num; i++) {
		if ((fmt->axes & BIT(first + i)) != 0U) {
			reading->values[i] = adxl372_axis_get(frame, fmt->axes, first + i);
		}
	}

	return 1;
}

static int adxl372_get_frames(const uint8_t *buffer, struct sensor_raw_frames *frames,
			      struct adxl372_frame_format *fmt)
{
	const struct adxl372_fifo_data *hdr = (const struct adxl372_fifo_data *)buffer;
	const struct adxl372_xyz_accel_data *sample = (const struct adxl372_xyz_accel_data *)buffer;

	*frames = (struct sensor_raw_frames){
		.decode_frame = adxl372_decode_frame,
		.user_data = fmt,
		.shift = ADXL372_SHIFT,
	};

	if (IS_ENABLED(CONFIG_ADXL372_STREAM) && hdr->is_fifo == 1U) {
		fmt->axes = (hdr->has_x == 1U ? BIT(0) : 0U) | (hdr->has_y == 1U ? BIT(1) : 0U) |
			    (hdr->has_z == 1U ? BIT(2) : 0U);

		if (hdr->accel_odr >= ARRAY_SIZE(accel_period_ns)) {
			return -EINVAL;
		}

		frames->frames = buffer + sizeof(*hdr);
		frames->size = hdr->fifo_byte_count;
		frames->timestamp_ns = hdr->timestamp;
		frames->period_ns = accel_period_ns[hdr->accel_odr];

		if (frames->size == 0U) {
			/* Buffers completed without data record no frame format */
			frames->frame_size = ADXL372_NUM_AXES * ADXL372_AXIS_SIZE;
		} else if (hdr->sample_set_size < adxl372_axes_size(fmt->axes)) {
			return -EINVAL;
		} else {
			frames->frame_size = hdr->sample_set_size;
		}
	} else {
		/* The single sample holds left-justified 12-bit values in CPU byte order */
		fmt->axes = BIT_MASK(ADXL372_NUM_AXES);
		sys_put_be16((uint16_t)sample->x, &fmt->sample[0]);
		sys_put_be16((uint16_t)sample->y, &fmt->sample[2]);
		sys_put_be16((uint16_t)sample->z, &fmt->sample[4]);

		frames->frames = fmt->sample;
		frames->size = sizeof(fmt->sample);
		frames->frame_size = sizeof(fmt->sample);
	}

	return 0;
}

static int adxl372_decoder_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
					   uint16_t *frame_count)
{
	struct sensor_raw_frames frames;
	struct adxl372_frame_format fmt;
	int rc;

	if (!adxl372_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	rc = adxl372_get_frames(buffer, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int adxl372_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				  uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_raw_frames frames;
	struct adxl372_frame_format fmt;
	int rc;

	if (!adxl372_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	rc = adxl372_get_frames(buffer, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

static bool adxl372_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	const struct adxl372_fifo_data *data = (const struct adxl372_fifo_data *)buffer;

	if (!IS_ENABLED(CONFIG_ADXL372_STREAM) || data->is_fifo == 0U) {
		return false;
	}

	/* The STATUS_1 bits have the positions of the INT1_MAP bits */
	switch (trigger) {
	case SENSOR_TRIG_DATA_READY:
		return FIELD_GET(ADXL372_INT1_MAP_DATA_RDY_MSK, data->int_status) != 0U;
	case SENSOR_TRIG_FIFO_WATERMARK:
	case SENSOR_TRIG_FIFO_FULL:
		return FIELD_GET(ADXL372_INT1_MAP_FIFO_FULL_MSK, data->int_status) != 0U;
	default:
		return false;
	}
}

static int adxl372_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					 size_t *frame_size)
{
	if (!adxl372_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 0U, base_size, frame_size);
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = adxl372_decoder_get_frame_count,
	.get_size_info = adxl372_decoder_get_size_info,
	.decode = adxl372_decoder_decode,
	.has_trigger = adxl372_decoder_has_trigger,
};

int adxl372_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
