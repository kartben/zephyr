/*
 * Copyright (c) 2026 Analog Devices Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/sys/byteorder.h>

#include "adxl355.h"

#define DT_DRV_COMPAT adi_adxl355

/* Shift of the q31 output values, for all ranges */
#define ADXL355_Q31_SHIFT 11

/* Samples are 20-bit two's complement, left-justified in 3 big-endian bytes */
#define ADXL355_SAMPLE_BITS   20U
#define ADXL355_ENTRY_SIZE    3U
#define ADXL355_FRAME_SIZE    (3U * ADXL355_ENTRY_SIZE)
/* Bit of the last byte of a FIFO entry set for the X axis */
#define ADXL355_FIFO_X_MARKER BIT(0)

/*
 * One LSB is SENSOR_G / 256000 um/s^2 at +/-2 g and doubles at each range step. The
 * denominator 256000 * 10^6 does not fit in 32 bits: it is divided by 2^16 and the shift
 * passed to sensor_raw_to_q31_ratio() is raised by 16 to compensate.
 */
#define ADXL355_LSB_DEN ((ADXL355_SENSITIVITY_2G * 1000000LL) >> 16)

static const int8_t range_to_ratio_shift[] = {
	[ADXL355_RANGE_2G] = ADXL355_Q31_SHIFT + 16,
	[ADXL355_RANGE_4G] = ADXL355_Q31_SHIFT + 15,
	[ADXL355_RANGE_8G] = ADXL355_Q31_SHIFT + 14,
};

/* The output data rate is 4000 Hz divided by 2^odr */
#define ADXL355_PERIOD_NS_4000HZ SENSOR_ODR_MHZ_TO_PERIOD_NS(4000000)

/* A single sample holds the raw bytes of each axis at the start of the x, y and z fields */
BUILD_ASSERT(offsetof(struct adxl355_sample, y) - offsetof(struct adxl355_sample, x) ==
	     sizeof(int32_t));
BUILD_ASSERT(offsetof(struct adxl355_sample, z) - offsetof(struct adxl355_sample, y) ==
	     sizeof(int32_t));

struct adxl355_frame_format {
	/* Distance between the raw bytes of two axes */
	uint8_t stride;
	int8_t ratio_shift;
};

static bool adxl355_chan_is_supported(struct sensor_chan_spec chan_spec)
{
	return chan_spec.chan_idx == 0U && SENSOR_CHANNEL_IS_ACCEL(chan_spec.chan_type);
}

static int adxl355_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				const void *user_data, struct sensor_frame_reading *reading)
{
	const struct adxl355_frame_format *fmt = user_data;
	uint8_t first;
	uint8_t num;

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_ACCEL_XYZ:
		first = 0U;
		num = 3U;
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

	if (reading == NULL) {
		return 1;
	}

	for (uint8_t i = 0U; i < num; i++) {
		uint32_t raw = sys_get_be24(&frame[(first + i) * fmt->stride]) >> 4;

		reading->values[i] = sensor_raw_to_q31_ratio(raw, ADXL355_SAMPLE_BITS, SENSOR_G,
							     ADXL355_LSB_DEN, fmt->ratio_shift);
	}

	return 1;
}

/* Get the offset of the first X axis entry among the first three FIFO entries */
static size_t adxl355_fifo_x_offset(const uint8_t *entries, size_t size)
{
	for (size_t pos = 0U; pos < ADXL355_FRAME_SIZE && pos + ADXL355_ENTRY_SIZE <= size;
	     pos += ADXL355_ENTRY_SIZE) {
		if ((entries[pos + 2U] & ADXL355_FIFO_X_MARKER) != 0U) {
			return pos;
		}
	}

	return 0U;
}

static int adxl355_get_frames(const uint8_t *buffer, struct sensor_raw_frames *frames,
			      struct adxl355_frame_format *fmt)
{
	const struct adxl355_fifo_data *hdr = (const struct adxl355_fifo_data *)buffer;
	const struct adxl355_sample *sample = (const struct adxl355_sample *)buffer;
	uint32_t range;

	*frames = (struct sensor_raw_frames){
		.decode_frame = adxl355_decode_frame,
		.user_data = fmt,
		.shift = ADXL355_Q31_SHIFT,
	};

	if (IS_ENABLED(CONFIG_ADXL355_STREAM) && hdr->is_fifo == 1U) {
		const uint8_t *entries = buffer + sizeof(*hdr);
		size_t x_offset;

		fmt->stride = ADXL355_ENTRY_SIZE;
		frames->frames = entries;
		frames->frame_size = ADXL355_FRAME_SIZE;
		frames->timestamp_ns = hdr->timestamp;

		if (hdr->fifo_byte_count == 0U) {
			/* SENSOR_STREAM_DATA_NOP or _DROP: range and ODR are not set */
			return 0;
		}

		if (hdr->accel_odr > ADXL355_ODR_3_906HZ) {
			return -EINVAL;
		}

		x_offset = adxl355_fifo_x_offset(entries, hdr->fifo_byte_count);
		range = hdr->range;
		frames->frames += x_offset;
		frames->size = hdr->fifo_byte_count - x_offset;
		frames->period_ns = ADXL355_PERIOD_NS_4000HZ << hdr->accel_odr;
	} else {
		range = (uint32_t)sample->range;
		fmt->stride = sizeof(sample->x);
		frames->frames = (const uint8_t *)&sample->x;
		frames->size = 3U * sizeof(sample->x);
		frames->frame_size = frames->size;
		frames->timestamp_ns = k_ticks_to_ns_floor64(k_uptime_ticks());
	}

	if (range < ADXL355_RANGE_2G || range > ADXL355_RANGE_8G) {
		return -EINVAL;
	}

	fmt->ratio_shift = range_to_ratio_shift[range];

	return 0;
}

static int adxl355_decoder_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
					   uint16_t *frame_count)
{
	struct sensor_raw_frames frames;
	struct adxl355_frame_format fmt;
	int rc;

	if (!adxl355_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	rc = adxl355_get_frames(buffer, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int adxl355_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				  uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_raw_frames frames;
	struct adxl355_frame_format fmt;
	int rc;

	if (!adxl355_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	rc = adxl355_get_frames(buffer, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

static int adxl355_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
				 size_t *frame_size)
{
	if (!adxl355_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 0U, base_size, frame_size);
}

static bool adxl355_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	const struct adxl355_fifo_data *fifo_data = (const struct adxl355_fifo_data *)buffer;

	if (!IS_ENABLED(CONFIG_ADXL355_STREAM) || fifo_data->is_fifo == 0U) {
		return false;
	}

	switch (trigger) {
	case SENSOR_TRIG_FIFO_WATERMARK:
	case SENSOR_TRIG_FIFO_FULL:
		return FIELD_GET(ADXL355_STATUS_FIFO_FULL_MSK, fifo_data->status1) != 0U;
	default:
		return false;
	}
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = adxl355_decoder_get_frame_count,
	.get_size_info = adxl355_get_size_info,
	.decode = adxl355_decoder_decode,
	.has_trigger = adxl355_decoder_has_trigger,
};

/**
 * @brief Get sensor decoder
 *
 * @param dev Device pointer
 * @param decoder Decoder API pointer
 * @return int 0 on success, negative error code otherwise
 */
int adxl355_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
