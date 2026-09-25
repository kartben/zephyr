/*
 * Copyright (c) 2024 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor_decoder.h>

#include "bme280.h"

/* Scaling of a compensated reading: one LSB is 1/den of the channel unit */
struct bme280_chan_format {
	int32_t den;
	int8_t shift;
};

/* comp_temp is in 0.01 degC */
static const struct bme280_chan_format temp_format = {
	.den = BME280_TEMP_CONV,
	.shift = BME280_TEMP_SHIFT,
};

/* comp_press is in UQ24.8 Pa, decoded in kPa */
static const struct bme280_chan_format press_format = {
	.den = 256 * BME280_PRESS_CONV_KPA,
	.shift = BME280_PRESS_SHIFT,
};

/* comp_humidity is in UQ22.10 %RH */
static const struct bme280_chan_format hum_format = {
	.den = 1024,
	.shift = BME280_HUM_SHIFT,
};

static const struct bme280_chan_format *bme280_chan_format_get(struct sensor_chan_spec chan_spec)
{
	if (chan_spec.chan_idx != 0U) {
		return NULL;
	}

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_AMBIENT_TEMP:
		return &temp_format;
	case SENSOR_CHAN_PRESS:
		return &press_format;
	case SENSOR_CHAN_HUMIDITY:
		return &hum_format;
	default:
		return NULL;
	}
}

/* The buffer holds one frame: the whole struct bme280_encoded_data */
static int bme280_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
			       const void *user_data, struct sensor_frame_reading *reading)
{
	const struct bme280_encoded_data *edata = (const struct bme280_encoded_data *)frame;
	const struct bme280_chan_format *fmt = user_data;
	bool present;
	uint32_t raw;

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_AMBIENT_TEMP:
		present = edata->has_temp == 1U;
		raw = (uint32_t)edata->reading.comp_temp;
		break;
	case SENSOR_CHAN_PRESS:
		present = edata->has_press == 1U;
		raw = edata->reading.comp_press;
		break;
	case SENSOR_CHAN_HUMIDITY:
		present = edata->has_humidity == 1U;
		raw = edata->reading.comp_humidity;
		break;
	default:
		return -ENOTSUP;
	}

	if (!present) {
		return 0;
	}

	if (reading != NULL) {
		reading->values[0] = sensor_raw_to_q31_ratio(raw, 32U, 1, fmt->den, fmt->shift);
	}

	return 1;
}

static void bme280_get_frames(const uint8_t *buffer, const struct bme280_chan_format *fmt,
			      struct sensor_raw_frames *frames)
{
	const struct bme280_encoded_data *edata = (const struct bme280_encoded_data *)buffer;

	*frames = (struct sensor_raw_frames){
		.frames = buffer,
		.size = sizeof(*edata),
		.frame_size = sizeof(*edata),
		.decode_frame = bme280_decode_frame,
		.user_data = fmt,
		.timestamp_ns = edata->header.timestamp,
		.shift = fmt->shift,
	};
}

/*
 * A channel that was not read, such as the humidity of a BMP280, is reported as not supported,
 * as the default decoder does, so that callers iterating over channels skip it.
 */
static int bme280_decoder_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
					  uint16_t *frame_count)
{
	const struct bme280_chan_format *fmt = bme280_chan_format_get(chan_spec);
	struct sensor_raw_frames frames;
	int rc;

	if (fmt == NULL) {
		return -ENOTSUP;
	}

	bme280_get_frames(buffer, fmt, &frames);

	rc = sensor_raw_frames_count(&frames, chan_spec, frame_count);
	if (rc != 0) {
		return rc;
	}

	return (*frame_count > 0U) ? 0 : -ENOTSUP;
}

static int bme280_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					size_t *frame_size)
{
	if (bme280_chan_format_get(chan_spec) == NULL) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 0U, base_size, frame_size);
}

static int bme280_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				 uint32_t *fit, uint16_t max_count, void *data_out)
{
	const struct bme280_chan_format *fmt = bme280_chan_format_get(chan_spec);
	struct sensor_raw_frames frames;
	uint16_t frame_count;
	int rc;

	if (fmt == NULL) {
		return -ENOTSUP;
	}

	bme280_get_frames(buffer, fmt, &frames);

	rc = sensor_raw_frames_count(&frames, chan_spec, &frame_count);
	if (rc != 0) {
		return rc;
	}

	if (frame_count == 0U) {
		return -ENODATA;
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = bme280_decoder_get_frame_count,
	.get_size_info = bme280_decoder_get_size_info,
	.decode = bme280_decoder_decode,
};

int bme280_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
