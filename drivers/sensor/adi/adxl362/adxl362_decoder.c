/*
 * Copyright (c) 2024 Analog Devices Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT adi_adxl362

#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/sys/byteorder.h>

#include "adxl362.h"

/* Accelerometer and temperature samples are 12-bit two's complement */
#define ADXL362_SAMPLE_BITS 12U

/* Index of the temperature in a frame, equal to its FIFO tag */
#define ADXL362_TEMP_IDX 3U

#define ADXL362_TEMP_SHIFT 8

#define ADXL362_Q31_SCALE(lsb_per_g, shift)                                                        \
	SENSOR_Q31_SCALE(SENSOR_G, (lsb_per_g) * 1000000LL, (shift))

static const uint64_t accel_period_ns[] = {
	[ADXL362_ODR_12_5_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(12500),
	[ADXL362_ODR_25_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(25000),
	[ADXL362_ODR_50_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(50000),
	[ADXL362_ODR_100_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(100000),
	[ADXL362_ODR_200_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(200000),
	[ADXL362_ODR_400_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(400000),
};

static const int8_t range_to_shift[] = {
	[ADXL362_RANGE_2G] = 5,
	[ADXL362_RANGE_4G] = 6,
	[ADXL362_RANGE_8G] = 7,
};

static const int32_t qscale_factor[] = {
	[ADXL362_RANGE_2G] = ADXL362_Q31_SCALE(ADXL362_ACCEL_2G_LSB_PER_G, 5),
	[ADXL362_RANGE_4G] = ADXL362_Q31_SCALE(ADXL362_ACCEL_4G_LSB_PER_G, 6),
	[ADXL362_RANGE_8G] = ADXL362_Q31_SCALE(ADXL362_ACCEL_8G_LSB_PER_G, 7),
};

struct adxl362_frame_format {
	int32_t scale;
	/* Frames hold a temperature after the three axes */
	bool has_temp;
	/* Frames are FIFO sample sets, whose entries carry an axis tag */
	bool is_fifo;
};

static bool adxl362_chan_is_supported(struct sensor_chan_spec chan_spec)
{
	if (chan_spec.chan_idx != 0U) {
		return false;
	}

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z:
	case SENSOR_CHAN_ACCEL_XYZ:
	case SENSOR_CHAN_DIE_TEMP:
		return true;
	default:
		return false;
	}
}

static q31_t adxl362_temp_to_q31(uint16_t raw)
{
	/* See sensitivity and bias specifications in table 1 of datasheet */
	int32_t milli_c = (sign_extend(raw, ADXL362_SAMPLE_BITS - 1U) - ADXL362_TEMP_BIAS_LSB) *
				  ADXL362_TEMP_MC_PER_LSB +
			  ADXL362_TEMP_BIAS_TEST_CONDITION * 1000;

	return sensor_raw_to_q31_ratio((uint32_t)milli_c, 32U, 1, 1000, ADXL362_TEMP_SHIFT);
}

static int adxl362_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				const void *user_data, struct sensor_frame_reading *reading)
{
	const struct adxl362_frame_format *fmt = user_data;
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
	case SENSOR_CHAN_DIE_TEMP:
		if (!fmt->has_temp) {
			return -ENOTSUP;
		}
		first = ADXL362_TEMP_IDX;
		num = 1U;
		break;
	default:
		return -ENOTSUP;
	}

	if (reading == NULL) {
		return 1;
	}

	for (uint8_t i = 0U; i < num; i++) {
		const uint8_t idx = first + i;
		const uint16_t raw = sys_get_le16(&frame[idx * 2U]);

		/* An entry with the wrong tag means that the FIFO read was misaligned */
		if (fmt->is_fifo && FIELD_GET(ADXL362_FIFO_TAG_MSK, raw) != idx) {
			return -ENODATA;
		}

		if (idx == ADXL362_TEMP_IDX) {
			reading->values[i] = adxl362_temp_to_q31(raw);
		} else {
			reading->values[i] =
				sensor_raw_to_q31(raw, ADXL362_SAMPLE_BITS, fmt->scale);
		}
	}

	return 1;
}

static int adxl362_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
			      struct sensor_raw_frames *frames, struct adxl362_frame_format *fmt)
{
	const struct adxl362_fifo_data *hdr = (const struct adxl362_fifo_data *)buffer;
	const struct adxl362_sample_data *sample = (const struct adxl362_sample_data *)buffer;
	uint8_t range;

	*frames = (struct sensor_raw_frames){
		.decode_frame = adxl362_decode_frame,
		.user_data = fmt,
	};

	if (IS_ENABLED(CONFIG_ADXL362_STREAM) && hdr->is_fifo == 1U) {
		if (hdr->accel_odr >= ARRAY_SIZE(accel_period_ns)) {
			return -EINVAL;
		}

		range = hdr->selected_range;
		fmt->has_temp = hdr->has_tmp == 1U;
		fmt->is_fifo = true;
		frames->frames = buffer + sizeof(*hdr);
		frames->size = hdr->fifo_byte_count;
		frames->frame_size =
			fmt->has_temp ? ADXL362_SAMPLE_SIZE : ADXL362_SAMPLE_SIZE_ACCEL;
		frames->period_ns = accel_period_ns[hdr->accel_odr];
		frames->timestamp_ns = hdr->timestamp;
	} else {
		range = sample->selected_range;
		fmt->has_temp = true;
		fmt->is_fifo = false;
		frames->frames = sample->raw;
		frames->size = sizeof(sample->raw);
		frames->frame_size = sizeof(sample->raw);
		frames->timestamp_ns = sample->timestamp;
	}

	if (range >= ARRAY_SIZE(range_to_shift)) {
		return -EINVAL;
	}

	fmt->scale = qscale_factor[range];
	frames->shift = (chan_spec.chan_type == SENSOR_CHAN_DIE_TEMP) ? ADXL362_TEMP_SHIFT
								      : range_to_shift[range];

	return 0;
}

static int adxl362_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					 size_t *frame_size)
{
	if (!adxl362_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 0U, base_size, frame_size);
}

static int adxl362_decoder_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
					   uint16_t *frame_count)
{
	struct sensor_raw_frames frames;
	struct adxl362_frame_format fmt;
	int rc;

	if (!adxl362_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	rc = adxl362_get_frames(buffer, chan_spec, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int adxl362_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				  uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_raw_frames frames;
	struct adxl362_frame_format fmt;
	int rc;

	if (!adxl362_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	rc = adxl362_get_frames(buffer, chan_spec, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

static bool adxl362_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	const struct adxl362_fifo_data *data = (const struct adxl362_fifo_data *)buffer;

	if (!IS_ENABLED(CONFIG_ADXL362_STREAM) || data->is_fifo == 0U) {
		return false;
	}

	switch (trigger) {
	case SENSOR_TRIG_DATA_READY:
		return ADXL362_STATUS_CHECK_DATA_READY(data->int_status) != 0U;
	case SENSOR_TRIG_FIFO_WATERMARK:
		return ADXL362_STATUS_CHECK_FIFO_WTR(data->int_status) != 0U;
	case SENSOR_TRIG_FIFO_FULL:
		return ADXL362_STATUS_CHECK_FIFO_OVR(data->int_status) != 0U;
	default:
		return false;
	}
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = adxl362_decoder_get_frame_count,
	.get_size_info = adxl362_decoder_get_size_info,
	.decode = adxl362_decoder_decode,
	.has_trigger = adxl362_decoder_has_trigger,
};

int adxl362_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
