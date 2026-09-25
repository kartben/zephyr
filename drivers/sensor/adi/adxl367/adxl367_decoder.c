/*
 * Copyright (c) 2024 Analog Devices Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/sys/byteorder.h>

#include "adxl367.h"

/* Samples of all read modes are converted to the 14-bit full resolution */
#define ADXL367_SAMPLE_BITS 14U

/*
 * The sensitivity halves at each range step while the shift grows by one, so the scale
 * factor is the same for all ranges.
 */
#define ADXL367_ACCEL_QSCALE SENSOR_Q31_SCALE(SENSOR_G, ADXL367_ACCEL_2G_LSB_PER_G * 1000000LL, 5)

#define ADXL367_TEMP_SHIFT   8
/* Temperature at ADXL367_TEMP_25C counts, in q31 format */
#define ADXL367_TEMP_Q31_25C SENSOR_Q31_SCALE(25, 1, ADXL367_TEMP_SHIFT)

/* Samples of a packet, indexed by FIFO channel ID */
enum adxl367_sample {
	ADXL367_SAMPLE_X,
	ADXL367_SAMPLE_Y,
	ADXL367_SAMPLE_Z,
	ADXL367_SAMPLE_TEMP,
	ADXL367_SAMPLE_COUNT,
};

/* Position of a sample absent from the packets */
#define ADXL367_NO_SAMPLE UINT8_MAX

static const int8_t range_to_shift[] = {
	[ADXL367_2G_RANGE] = 5,
	[ADXL367_4G_RANGE] = 6,
	[ADXL367_8G_RANGE] = 7,
};

static const uint64_t accel_period_ns[] = {
	[ADXL367_ODR_12P5HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(12500),
	[ADXL367_ODR_25HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(25000),
	[ADXL367_ODR_50HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(50000),
	[ADXL367_ODR_100HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(100000),
	[ADXL367_ODR_200HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(200000),
	[ADXL367_ODR_400HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(400000),
};

struct adxl367_frame_format {
	/* Frames are FIFO packets rather than a struct adxl367_sample_data */
	bool is_fifo;
	/* FIFO read mode, enum adxl367_fifo_read_mode */
	uint8_t read_mode;
	/* Number of samples in a FIFO packet */
	uint8_t packet_samples;
	/* Number of FIFO packets in a frame */
	uint8_t packets;
	/* Position of each sample in a packet, or ADXL367_NO_SAMPLE */
	uint8_t pos[ADXL367_SAMPLE_COUNT];
};

static bool adxl367_chan_is_supported(struct sensor_chan_spec chan_spec)
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

static q31_t adxl367_temp_to_q31(uint32_t raw)
{
	int32_t counts = sign_extend(raw, ADXL367_SAMPLE_BITS - 1U) - ADXL367_TEMP_25C;

	return sensor_raw_to_q31_ratio((uint32_t)counts, 32U, 1, ADXL367_TEMP_SENSITIVITY,
				       ADXL367_TEMP_SHIFT) +
	       ADXL367_TEMP_Q31_25C;
}

/* Get sample of a FIFO frame as a 14-bit value, false if its channel ID does not match */
static bool adxl367_fifo_sample_get(const struct adxl367_frame_format *fmt, const uint8_t *frame,
				    uint8_t packet, enum adxl367_sample sample, uint32_t *raw)
{
	const size_t n = (size_t)packet * fmt->packet_samples + fmt->pos[sample];
	const uint8_t *p;
	uint16_t word;

	switch (fmt->read_mode) {
	case ADXL367_8B:
		/* 8 MSBs of the sample */
		*raw = (uint32_t)frame[n] << 6;
		return true;
	case ADXL367_12B:
		/* 12 MSBs packed MSB first: odd samples start in the middle of a byte */
		p = &frame[n * 12U / 8U];
		if ((n % 2U) == 0U) {
			*raw = ((uint32_t)p[0] << 4) | ((uint32_t)p[1] >> 4);
		} else {
			*raw = ((uint32_t)(p[0] & 0x0FU) << 8) | p[1];
		}
		*raw <<= 2;
		return true;
	case ADXL367_12B_CHID:
		/* Channel ID in bits 15:14, 12 MSBs of the sample in bits 11:0 */
		word = sys_get_le16(&frame[n * 2U]);
		*raw = (uint32_t)(word & GENMASK(11, 0)) << 2;
		break;
	case ADXL367_14B_CHID:
		/* Channel ID in bits 15:14, sample in bits 13:0 */
		word = sys_get_be16(&frame[n * 2U]);
		*raw = word & GENMASK(13, 0);
		break;
	default:
		return false;
	}

	return ADXL367_FIFO_HDR_GET_ACCEL_AXIS(word) == (uint16_t)sample;
}

static void adxl367_single_sample_get(const uint8_t *frame, enum adxl367_sample sample,
				      uint32_t *raw)
{
	const struct adxl367_sample_data *data = (const struct adxl367_sample_data *)frame;

	switch (sample) {
	case ADXL367_SAMPLE_X:
		*raw = (uint16_t)data->xyz.x;
		break;
	case ADXL367_SAMPLE_Y:
		*raw = (uint16_t)data->xyz.y;
		break;
	case ADXL367_SAMPLE_Z:
		*raw = (uint16_t)data->xyz.z;
		break;
	default:
		*raw = (uint16_t)data->raw_temp;
		break;
	}
}

static int adxl367_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				const void *user_data, struct sensor_frame_reading *reading)
{
	const struct adxl367_frame_format *fmt = user_data;
	enum adxl367_sample first;
	uint8_t num;
	bool present = false;

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_ACCEL_XYZ:
		first = ADXL367_SAMPLE_X;
		num = 3U;
		break;
	case SENSOR_CHAN_ACCEL_X:
		first = ADXL367_SAMPLE_X;
		num = 1U;
		break;
	case SENSOR_CHAN_ACCEL_Y:
		first = ADXL367_SAMPLE_Y;
		num = 1U;
		break;
	case SENSOR_CHAN_ACCEL_Z:
		first = ADXL367_SAMPLE_Z;
		num = 1U;
		break;
	case SENSOR_CHAN_DIE_TEMP:
		first = ADXL367_SAMPLE_TEMP;
		num = 1U;
		break;
	default:
		return -ENOTSUP;
	}

	/*
	 * Frames hold no reading of a channel absent from the FIFO packets. An axis missing
	 * from them reads as 0 in SENSOR_CHAN_ACCEL_XYZ.
	 */
	for (uint8_t i = 0U; i < num; i++) {
		present = present || fmt->pos[first + i] != ADXL367_NO_SAMPLE;
	}

	if (!present) {
		return 0;
	}

	if (reading == NULL) {
		return fmt->packets;
	}

	for (uint8_t i = 0U; i < num; i++) {
		enum adxl367_sample sample = first + i;
		uint32_t raw;

		if (fmt->pos[sample] == ADXL367_NO_SAMPLE) {
			continue;
		}

		if (!fmt->is_fifo) {
			adxl367_single_sample_get(frame, sample, &raw);
		} else if (!adxl367_fifo_sample_get(fmt, frame, reading->index, sample, &raw)) {
			/* Unexpected channel ID: the value stays 0 */
			continue;
		}

		if (sample == ADXL367_SAMPLE_TEMP) {
			reading->values[i] = adxl367_temp_to_q31(raw);
		} else {
			reading->values[i] =
				sensor_raw_to_q31(raw, ADXL367_SAMPLE_BITS, ADXL367_ACCEL_QSCALE);
		}
	}

	return fmt->packets;
}

static int adxl367_get_fifo_frames(const struct adxl367_fifo_data *hdr,
				   struct sensor_raw_frames *frames,
				   struct adxl367_frame_format *fmt)
{
	uint8_t samples = 0U;
	size_t packet_size;

	frames->frames = (const uint8_t *)hdr + sizeof(*hdr);
	frames->size = hdr->fifo_byte_count;
	frames->timestamp_ns = hdr->timestamp;

	if (hdr->fifo_byte_count == 0U) {
		/* Buffer completed without data, for SENSOR_STREAM_DATA_NOP or _DROP */
		frames->frame_size = 1U;
		return 0;
	}

	if (hdr->accel_odr >= ARRAY_SIZE(accel_period_ns)) {
		return -EINVAL;
	}

	frames->period_ns = accel_period_ns[hdr->accel_odr];

	/* Packets hold the enabled samples in the order X, Y, Z, then temperature or ADC */
	if (hdr->has_x == 1U) {
		fmt->pos[ADXL367_SAMPLE_X] = samples++;
	}
	if (hdr->has_y == 1U) {
		fmt->pos[ADXL367_SAMPLE_Y] = samples++;
	}
	if (hdr->has_z == 1U) {
		fmt->pos[ADXL367_SAMPLE_Z] = samples++;
	}
	if (hdr->has_tmp == 1U) {
		fmt->pos[ADXL367_SAMPLE_TEMP] = samples++;
	}
	if (hdr->has_adc == 1U) {
		samples++;
	}

	fmt->is_fifo = true;
	fmt->read_mode = hdr->fifo_read_mode;
	fmt->packet_samples = samples;

	switch (hdr->fifo_read_mode) {
	case ADXL367_8B:
		packet_size = samples;
		break;
	case ADXL367_12B_CHID:
	case ADXL367_14B_CHID:
		packet_size = samples * 2U;
		break;
	case ADXL367_12B:
		/*
		 * packet_size holds the number of samples. Packets of an odd number of 12-bit
		 * samples do not end on a byte boundary, so a frame holds two of them.
		 */
		packet_size = samples;
		fmt->packets = ((samples % 2U) == 0U) ? 1U : 2U;
		break;
	default:
		return -EINVAL;
	}

	if (samples == 0U || hdr->packet_size != packet_size) {
		return -EINVAL;
	}

	if (hdr->fifo_read_mode == ADXL367_12B) {
		frames->frame_size = (size_t)samples * fmt->packets * 12U / 8U;
	} else {
		frames->frame_size = packet_size;
	}

	return 0;
}

static int adxl367_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
			      struct sensor_raw_frames *frames, struct adxl367_frame_format *fmt)
{
	const struct adxl367_fifo_data *hdr = (const struct adxl367_fifo_data *)buffer;
	uint8_t range;
	int rc;

	*frames = (struct sensor_raw_frames){
		.decode_frame = adxl367_decode_frame,
		.user_data = fmt,
	};
	*fmt = (struct adxl367_frame_format){
		.packets = 1U,
	};

	if (IS_ENABLED(CONFIG_ADXL367_STREAM) && hdr->is_fifo == 1U) {
		for (uint8_t i = 0U; i < ARRAY_SIZE(fmt->pos); i++) {
			fmt->pos[i] = ADXL367_NO_SAMPLE;
		}

		rc = adxl367_get_fifo_frames(hdr, frames, fmt);
		if (rc != 0) {
			return rc;
		}

		range = hdr->range;
	} else {
		const struct adxl367_sample_data *sample =
			(const struct adxl367_sample_data *)buffer;

		for (uint8_t i = 0U; i < ARRAY_SIZE(fmt->pos); i++) {
			fmt->pos[i] = i;
		}

		frames->frames = buffer;
		frames->size = sizeof(*sample);
		frames->frame_size = sizeof(*sample);
		frames->timestamp_ns = sample->timestamp;
		range = sample->xyz.range;
	}

	if (chan_spec.chan_type == SENSOR_CHAN_DIE_TEMP) {
		frames->shift = ADXL367_TEMP_SHIFT;
	} else if (range < ARRAY_SIZE(range_to_shift)) {
		frames->shift = range_to_shift[range];
	} else {
		return -EINVAL;
	}

	return 0;
}

static int adxl367_decoder_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
					   uint16_t *frame_count)
{
	struct sensor_raw_frames frames;
	struct adxl367_frame_format fmt;
	int rc;

	if (!adxl367_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	rc = adxl367_get_frames(buffer, chan_spec, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int adxl367_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				  uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_raw_frames frames;
	struct adxl367_frame_format fmt;
	int rc;

	if (!adxl367_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	rc = adxl367_get_frames(buffer, chan_spec, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

static bool adxl367_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	const struct adxl367_fifo_data *data = (const struct adxl367_fifo_data *)buffer;

	if (!IS_ENABLED(CONFIG_ADXL367_STREAM) || data->is_fifo == 0U) {
		return false;
	}

	switch (trigger) {
	case SENSOR_TRIG_DATA_READY:
		return (ADXL367_STATUS_DATA_RDY & data->int_status) != 0U;
	case SENSOR_TRIG_FIFO_WATERMARK:
		return (ADXL367_STATUS_FIFO_WATERMARK & data->int_status) != 0U;
	case SENSOR_TRIG_FIFO_FULL:
		return (ADXL367_STATUS_FIFO_OVERRUN & data->int_status) != 0U;
	default:
		return false;
	}
}

static int adxl367_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					 size_t *frame_size)
{
	if (!adxl367_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 0U, base_size, frame_size);
}

#define ADXL367_DECODER_API                                                                        \
	{                                                                                          \
		.get_frame_count = adxl367_decoder_get_frame_count,                                \
		.get_size_info = adxl367_decoder_get_size_info,                                    \
		.decode = adxl367_decoder_decode,                                                  \
		.has_trigger = adxl367_decoder_has_trigger,                                        \
	}

/* One decoder per compatible, as SENSOR_DECODER_DT_GET() expects */
#define DT_DRV_COMPAT adi_adxl366
SENSOR_DECODER_API_DT_DEFINE() = ADXL367_DECODER_API;
#undef DT_DRV_COMPAT

#define DT_DRV_COMPAT adi_adxl367
SENSOR_DECODER_API_DT_DEFINE() = ADXL367_DECODER_API;

int adxl367_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
