/*
 * Copyright (c) 2024 Analog Devices Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/sys/byteorder.h>

#include "adxl345.h"

/* Nominal sensitivity at +/-2 g, and in full resolution mode at any range */
#define ADXL345_LSB_PER_G 256

#define ADXL345_Q31_SCALE(lsb_per_g, shift)                                                        \
	SENSOR_Q31_SCALE(SENSOR_G, (lsb_per_g) * 1000000LL, (shift))

static const int8_t range_to_shift[] = {
	[ADXL345_RANGE_2G] = 5,
	[ADXL345_RANGE_4G] = 6,
	[ADXL345_RANGE_8G] = 7,
	[ADXL345_RANGE_16G] = 8,
};

/* Full resolution mode: 256 LSB/g, 10 to 13 bits depending on the range */
static const int32_t qscale_full_res[] = {
	[ADXL345_RANGE_2G] = ADXL345_Q31_SCALE(ADXL345_LSB_PER_G, 5),
	[ADXL345_RANGE_4G] = ADXL345_Q31_SCALE(ADXL345_LSB_PER_G, 6),
	[ADXL345_RANGE_8G] = ADXL345_Q31_SCALE(ADXL345_LSB_PER_G, 7),
	[ADXL345_RANGE_16G] = ADXL345_Q31_SCALE(ADXL345_LSB_PER_G, 8),
};

/*
 * 10-bit mode: the sensitivity halves at each range step while the shift grows by one, so the
 * scale factor is the same for all ranges.
 */
#define ADXL345_QSCALE_10BIT ADXL345_Q31_SCALE(ADXL345_LSB_PER_G, 5)

static const uint64_t accel_period_ns[] = {
	[ADXL345_ODR_12_5HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(12500),
	[ADXL345_ODR_25HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(25000),
	[ADXL345_ODR_50HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(50000),
	[ADXL345_ODR_100HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(100000),
	[ADXL345_ODR_200HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(200000),
	[ADXL345_ODR_400HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(400000),
};

struct adxl345_frame_format {
	uint8_t bits;
	int32_t scale;
};

static bool adxl345_chan_is_supported(struct sensor_chan_spec chan_spec)
{
	return chan_spec.chan_idx == 0U && SENSOR_CHANNEL_IS_ACCEL(chan_spec.chan_type);
}

static int adxl345_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				const void *user_data, struct sensor_frame_reading *reading)
{
	const struct adxl345_frame_format *fmt = user_data;
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
		reading->values[i] = sensor_raw_to_q31(sys_get_le16(&frame[(first + i) * 2U]),
						       fmt->bits, fmt->scale);
	}

	return 1;
}

static int adxl345_get_frames(const uint8_t *buffer, struct sensor_raw_frames *frames,
			      struct adxl345_frame_format *fmt)
{
	const struct adxl345_fifo_data *hdr = (const struct adxl345_fifo_data *)buffer;
	const struct adxl345_sample *sample = (const struct adxl345_sample *)buffer;
	uint8_t range;
	bool is_full_res;

	*frames = (struct sensor_raw_frames){
		.frame_size = SAMPLE_SIZE,
		.decode_frame = adxl345_decode_frame,
		.user_data = fmt,
	};

	if (IS_ENABLED(CONFIG_ADXL345_STREAM) && hdr->is_fifo == 1U) {
		if (hdr->accel_odr >= ARRAY_SIZE(accel_period_ns)) {
			return -EINVAL;
		}

		range = hdr->selected_range;
		is_full_res = hdr->is_full_res == 1U;
		frames->frames = buffer + sizeof(*hdr);
		frames->size = hdr->fifo_byte_count;
		frames->period_ns = accel_period_ns[hdr->accel_odr];
		frames->timestamp_ns = hdr->timestamp;
	} else {
		range = sample->selected_range;
		is_full_res = sample->is_full_res;
		frames->frames = sample->axis_data;
		frames->size = sizeof(sample->axis_data);
		frames->timestamp_ns = k_ticks_to_ns_floor64(k_uptime_ticks());
	}

	if (range >= ARRAY_SIZE(range_to_shift)) {
		return -EINVAL;
	}

	frames->shift = range_to_shift[range];
	fmt->bits = is_full_res ? 10U + range : 10U;
	fmt->scale = is_full_res ? qscale_full_res[range] : ADXL345_QSCALE_10BIT;

	return 0;
}

static int adxl345_decoder_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
					   uint16_t *frame_count)
{
	struct sensor_raw_frames frames;
	struct adxl345_frame_format fmt;
	int rc;

	if (!adxl345_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	rc = adxl345_get_frames(buffer, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int adxl345_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				  uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_raw_frames frames;
	struct adxl345_frame_format fmt;
	int rc;

	if (!adxl345_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	rc = adxl345_get_frames(buffer, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

static bool adxl345_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	const struct adxl345_fifo_data *data = (const struct adxl345_fifo_data *)buffer;

	if (!IS_ENABLED(CONFIG_ADXL345_STREAM) || data->is_fifo == 0U) {
		return false;
	}

	switch (trigger) {
	case SENSOR_TRIG_FIFO_WATERMARK:
		return FIELD_GET(ADXL345_INT_MAP_WATERMARK_MSK, data->int_status) != 0U;
	default:
		return false;
	}
}

static int adxl345_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
				 size_t *frame_size)
{
	if (!adxl345_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 0U, base_size, frame_size);
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = adxl345_decoder_get_frame_count,
	.decode = adxl345_decoder_decode,
	.has_trigger = adxl345_decoder_has_trigger,
	.get_size_info = adxl345_get_size_info,
};

int adxl345_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
