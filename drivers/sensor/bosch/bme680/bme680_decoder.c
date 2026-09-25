/*
 * Copyright (c) 2025 Alif Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_decoder.h>

#include "bme680.h"

/* Conversion of a compensated value: one LSB is 1 / den of the channel unit */
struct bme680_chan_conv {
	int32_t den;
	int8_t shift;
};

static int bme680_chan_conv_get(struct sensor_chan_spec chan_spec, struct bme680_chan_conv *conv)
{
	if (chan_spec.chan_idx != 0U) {
		return -ENOTSUP;
	}

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_AMBIENT_TEMP:
		/* 0.01 degC */
		*conv = (struct bme680_chan_conv){.den = 100, .shift = BME680_TEMP_SHIFT};
		return 0;
	case SENSOR_CHAN_PRESS:
		/* Pa, output in kPa */
		*conv = (struct bme680_chan_conv){.den = 1000, .shift = BME680_PRESS_SHIFT};
		return 0;
	case SENSOR_CHAN_HUMIDITY:
		/* 0.001 %RH */
		*conv = (struct bme680_chan_conv){.den = 1000, .shift = BME680_HUM_SHIFT};
		return 0;
	case SENSOR_CHAN_GAS_RES:
		/* ohm */
		*conv = (struct bme680_chan_conv){.den = 1, .shift = BME680_GAS_SHIFT};
		return 0;
	default:
		return -ENOTSUP;
	}
}

/* Unsigned values above INT32_MAX saturate in the q31 output anyway */
static uint32_t bme680_unsigned_clamp(uint32_t val)
{
	return MIN(val, (uint32_t)INT32_MAX);
}

static int bme680_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
			       const void *user_data, struct sensor_frame_reading *reading)
{
	const struct bme680_chan_conv *conv = user_data;
	struct bme680_encoded_data edata;
	bool present;
	uint32_t raw;

	/* The buffer is not necessarily aligned for struct bme680_encoded_data */
	memcpy(&edata, frame, sizeof(edata));

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_AMBIENT_TEMP:
		present = edata.has_temp != 0U;
		raw = (uint32_t)edata.reading.comp_temp;
		break;
	case SENSOR_CHAN_PRESS:
		present = edata.has_press != 0U;
		raw = bme680_unsigned_clamp(edata.reading.comp_press);
		break;
	case SENSOR_CHAN_HUMIDITY:
		present = edata.has_humidity != 0U;
		raw = bme680_unsigned_clamp(edata.reading.comp_humidity);
		break;
	case SENSOR_CHAN_GAS_RES:
		present = edata.has_gas != 0U;
		raw = bme680_unsigned_clamp(edata.reading.comp_gas);
		break;
	default:
		return -ENOTSUP;
	}

	if (!present) {
		return 0;
	}

	if (reading != NULL) {
		reading->values[0] = sensor_raw_to_q31_ratio(raw, 32U, 1, conv->den, conv->shift);
	}

	return 1;
}

static int bme680_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
			     struct sensor_raw_frames *frames, struct bme680_chan_conv *conv)
{
	struct bme680_decoder_header header;
	int rc;

	rc = bme680_chan_conv_get(chan_spec, conv);
	if (rc != 0) {
		return rc;
	}

	memcpy(&header, buffer, sizeof(header));

	/* A single sample: one frame, no period */
	*frames = (struct sensor_raw_frames){
		.frames = buffer,
		.size = sizeof(struct bme680_encoded_data),
		.frame_size = sizeof(struct bme680_encoded_data),
		.decode_frame = bme680_decode_frame,
		.user_data = conv,
		.timestamp_ns = header.timestamp,
		.shift = conv->shift,
		.num_values = 1U,
	};

	return 0;
}

static int bme680_decoder_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
					  uint16_t *frame_count)
{
	struct sensor_raw_frames frames;
	struct bme680_chan_conv conv;
	int rc;

	rc = bme680_get_frames(buffer, chan_spec, &frames, &conv);
	if (rc != 0) {
		return rc;
	}

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int bme680_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					size_t *frame_size)
{
	struct bme680_chan_conv conv;
	int rc;

	rc = bme680_chan_conv_get(chan_spec, &conv);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames_size_info(chan_spec, 1U, base_size, frame_size);
}

static int bme680_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				 uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_raw_frames frames;
	struct bme680_chan_conv conv;
	uint16_t frame_count;
	int rc;

	rc = bme680_get_frames(buffer, chan_spec, &frames, &conv);
	if (rc != 0) {
		return rc;
	}

	if (*fit == 0U) {
		rc = sensor_raw_frames_count(&frames, chan_spec, &frame_count);
		if (rc != 0) {
			return rc;
		}

		/* The channel was not part of this read */
		if (frame_count == 0U) {
			return -ENODATA;
		}
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = bme680_decoder_get_frame_count,
	.get_size_info = bme680_decoder_get_size_info,
	.decode = bme680_decoder_decode,
};

/* Get decoder API for device */
int bme680_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();
	return 0;
}
