/*
 * Copyright (c) 2024 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor_decoder.h>

#include "mmc56x3.h"

/* 20-bit magnetometer readings, 16384 LSB/Gauss */
#define MMC56X3_MAGN_BITS          20U
#define MMC56X3_MAGN_LSB_PER_GAUSS 16384

/* Factor scaling a magnetometer reading to q31 Gauss */
#define MMC56X3_MAGN_Q31_SCALE SENSOR_Q31_SCALE(1, MMC56X3_MAGN_LSB_PER_GAUSS, MMC56X3_MAGN_SHIFT)

static bool mmc56x3_chan_is_supported(struct sensor_chan_spec chan_spec)
{
	if (chan_spec.chan_idx != 0U) {
		return false;
	}

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_AMBIENT_TEMP:
	case SENSOR_CHAN_MAGN_X:
	case SENSOR_CHAN_MAGN_Y:
	case SENSOR_CHAN_MAGN_Z:
	case SENSOR_CHAN_MAGN_XYZ:
		return true;
	default:
		return false;
	}
}

/*
 * Check that the buffer holds a channel: 1 when it does, 0 when the channel was not read and
 * -ENOTSUP when the channel is not supported.
 */
static int mmc56x3_chan_present(const struct mmc56x3_encoded_data *edata,
				struct sensor_chan_spec chan_spec)
{
	bool present;

	if (!mmc56x3_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_AMBIENT_TEMP:
		present = edata->has_temp != 0U;
		break;
	case SENSOR_CHAN_MAGN_X:
		present = edata->has_magn_x != 0U;
		break;
	case SENSOR_CHAN_MAGN_Y:
		present = edata->has_magn_y != 0U;
		break;
	case SENSOR_CHAN_MAGN_Z:
		present = edata->has_magn_z != 0U;
		break;
	default:
		/* SENSOR_CHAN_MAGN_XYZ */
		present = edata->has_magn_x != 0U && edata->has_magn_y != 0U &&
			  edata->has_magn_z != 0U;
		break;
	}

	return present ? 1 : 0;
}

static q31_t mmc56x3_magn_to_q31(int32_t raw)
{
	return sensor_raw_to_q31((uint32_t)raw, MMC56X3_MAGN_BITS, MMC56X3_MAGN_Q31_SCALE);
}

/*
 * T = -75 degC + 0.8 degC * raw = (4 * raw - 375) / 5 degC. Saturates above 128 degC, for raw
 * values 254 and 255.
 */
static q31_t mmc56x3_temp_to_q31(uint8_t raw)
{
	return sensor_raw_to_q31_ratio((uint32_t)(4 * (int32_t)raw - 375), 32U, 1, 5,
				       MMC56X3_TEMP_SHIFT);
}

static int mmc56x3_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				const void *user_data, struct sensor_frame_reading *reading)
{
	const struct mmc56x3_encoded_data *edata = (const struct mmc56x3_encoded_data *)frame;
	const struct mmc56x3_data *data = &edata->data;
	int rc;

	ARG_UNUSED(user_data);

	rc = mmc56x3_chan_present(edata, chan_spec);
	if (rc <= 0 || reading == NULL) {
		return rc;
	}

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_AMBIENT_TEMP:
		reading->values[0] = mmc56x3_temp_to_q31((uint8_t)data->temp);
		break;
	case SENSOR_CHAN_MAGN_X:
		reading->values[0] = mmc56x3_magn_to_q31(data->magn_x);
		break;
	case SENSOR_CHAN_MAGN_Y:
		reading->values[0] = mmc56x3_magn_to_q31(data->magn_y);
		break;
	case SENSOR_CHAN_MAGN_Z:
		reading->values[0] = mmc56x3_magn_to_q31(data->magn_z);
		break;
	default:
		/* SENSOR_CHAN_MAGN_XYZ */
		reading->values[0] = mmc56x3_magn_to_q31(data->magn_x);
		reading->values[1] = mmc56x3_magn_to_q31(data->magn_y);
		reading->values[2] = mmc56x3_magn_to_q31(data->magn_z);
		break;
	}

	return 1;
}

/* This sensor lacks a FIFO: the buffer is a single frame */
static void mmc56x3_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
			       struct sensor_raw_frames *frames)
{
	const struct mmc56x3_encoded_data *edata = (const struct mmc56x3_encoded_data *)buffer;

	*frames = (struct sensor_raw_frames){
		.frames = buffer,
		.size = sizeof(struct mmc56x3_encoded_data),
		.frame_size = sizeof(struct mmc56x3_encoded_data),
		.decode_frame = mmc56x3_decode_frame,
		.timestamp_ns = edata->header.timestamp,
		.shift = (chan_spec.chan_type == SENSOR_CHAN_AMBIENT_TEMP) ? MMC56X3_TEMP_SHIFT
									   : MMC56X3_MAGN_SHIFT,
	};
}

static int mmc56x3_decoder_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
					   uint16_t *frame_count)
{
	struct sensor_raw_frames frames;
	int rc;

	rc = mmc56x3_chan_present((const struct mmc56x3_encoded_data *)buffer, chan_spec);
	if (rc < 0) {
		return rc;
	}

	if (rc == 0) {
		/* The channel was not read */
		*frame_count = 0U;
		return -ENOTSUP;
	}

	mmc56x3_get_frames(buffer, chan_spec, &frames);

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int mmc56x3_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					 size_t *frame_size)
{
	if (!mmc56x3_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 0U, base_size, frame_size);
}

static int mmc56x3_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				  uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_raw_frames frames;

	/* A channel that was not read gives -ENODATA, an unsupported one -ENOTSUP */
	mmc56x3_get_frames(buffer, chan_spec, &frames);

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = mmc56x3_decoder_get_frame_count,
	.get_size_info = mmc56x3_decoder_get_size_info,
	.decode = mmc56x3_decoder_decode,
};

int mmc56x3_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
