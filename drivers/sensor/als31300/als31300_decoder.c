/*
 * Copyright (c) 2025 Croxel
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT allegro_als31300

#include "als31300.h"

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/logging/log.h>
#include <zephyr/dsp/types.h>

LOG_MODULE_DECLARE(als31300, CONFIG_SENSOR_LOG_LEVEL);

/* One LSB is 1/4 G (ALS31300-500) */
#define ALS31300_MAGN_Q31_SCALE                                                                    \
	SENSOR_Q31_SCALE(1, ALS31300_SENSITIVITY_LSB_PER_GAUSS, ALS31300_MAGN_SHIFT)

/* One LSB above the offset is 302/4096 degrees Celsius */
#define ALS31300_TEMP_Q31_SCALE                                                                    \
	SENSOR_Q31_SCALE(ALS31300_TEMP_SCALE_FACTOR, ALS31300_TEMP_DIVISOR, ALS31300_TEMP_SHIFT)

BUILD_ASSERT(ALS31300_MAGN_SHIFT == ALS31300_TEMP_SHIFT,
	     "The magnetic field and temperature share the q31 shift");

/**
 * @brief Encode channel flags for the given sensor channel
 */
static uint8_t als31300_encode_channel(enum sensor_channel chan)
{
	uint8_t encode_bmask = 0;

	switch (chan) {
	case SENSOR_CHAN_MAGN_X:
		encode_bmask |= BIT(0);
		break;
	case SENSOR_CHAN_MAGN_Y:
		encode_bmask |= BIT(1);
		break;
	case SENSOR_CHAN_MAGN_Z:
		encode_bmask |= BIT(2);
		break;
	case SENSOR_CHAN_MAGN_XYZ:
		encode_bmask |= BIT(0) | BIT(1) | BIT(2);
		break;
	case SENSOR_CHAN_AMBIENT_TEMP:
		encode_bmask |= BIT(3);
		break;
	case SENSOR_CHAN_ALL:
		encode_bmask |= BIT(0) | BIT(1) | BIT(2) | BIT(3);
		break;
	default:
		break;
	}

	return encode_bmask;
}

static bool als31300_chan_is_supported(struct sensor_chan_spec chan_spec)
{
	if (chan_spec.chan_idx != 0U) {
		return false;
	}

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_MAGN_X:
	case SENSOR_CHAN_MAGN_Y:
	case SENSOR_CHAN_MAGN_Z:
	case SENSOR_CHAN_MAGN_XYZ:
	case SENSOR_CHAN_AMBIENT_TEMP:
		return true;
	default:
		return false;
	}
}

static int als31300_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				 const void *user_data, struct sensor_frame_reading *reading)
{
	struct als31300_readings readings;
	int16_t axes[3];
	uint8_t first;
	uint8_t num;

	ARG_UNUSED(user_data);

	if (chan_spec.chan_type == SENSOR_CHAN_AMBIENT_TEMP) {
		if (reading != NULL) {
			als31300_parse_registers(frame, &readings);
			/* |temp - offset| < 4096 and the scale is 2416: no overflow */
			reading->values[0] = ((int32_t)readings.temp - ALS31300_TEMP_OFFSET) *
					     ALS31300_TEMP_Q31_SCALE;
		}

		return 1;
	}

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_MAGN_X:
		first = 0U;
		num = 1U;
		break;
	case SENSOR_CHAN_MAGN_Y:
		first = 1U;
		num = 1U;
		break;
	case SENSOR_CHAN_MAGN_Z:
		first = 2U;
		num = 1U;
		break;
	case SENSOR_CHAN_MAGN_XYZ:
		first = 0U;
		num = 3U;
		break;
	default:
		return -ENOTSUP;
	}

	if (reading == NULL) {
		return 1;
	}

	als31300_parse_registers(frame, &readings);

	axes[0] = readings.x;
	axes[1] = readings.y;
	axes[2] = readings.z;

	for (uint8_t i = 0U; i < num; i++) {
		reading->values[i] =
			sensor_raw_to_q31((uint16_t)axes[first + i], 16U, ALS31300_MAGN_Q31_SCALE);
	}

	return 1;
}

/**
 * @brief Describe the encoded buffer for a channel
 *
 * @retval 0 Success
 * @retval -ENOTSUP Channel not supported
 * @retval -ENODATA Channel not requested in the read that produced the buffer
 */
static int als31300_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
			       struct sensor_raw_frames *frames)
{
	const struct als31300_encoded_data *edata = (const struct als31300_encoded_data *)buffer;
	uint8_t channel_request;

	if (!als31300_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	channel_request = als31300_encode_channel(chan_spec.chan_type);
	if ((edata->header.channels & channel_request) != channel_request) {
		return -ENODATA;
	}

	*frames = (struct sensor_raw_frames){
		.frames = edata->payload,
		.size = sizeof(edata->payload),
		.frame_size = sizeof(edata->payload),
		.decode_frame = als31300_decode_frame,
		.timestamp_ns = edata->header.timestamp,
		.shift = ALS31300_MAGN_SHIFT,
	};

	return 0;
}

/**
 * @brief Get frame count for decoder
 */
static int als31300_decoder_get_frame_count(const uint8_t *buffer,
					    struct sensor_chan_spec chan_spec,
					    uint16_t *frame_count)
{
	struct sensor_raw_frames frames;
	int rc;

	rc = als31300_get_frames(buffer, chan_spec, &frames);
	if (rc != 0) {
		return rc;
	}

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

/**
 * @brief Get size info for decoder
 */
static int als31300_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					  size_t *frame_size)
{
	if (!als31300_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 0U, base_size, frame_size);
}

/**
 * @brief Decode function for RTIO
 */
static int als31300_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				   uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_raw_frames frames;
	int rc;

	rc = als31300_get_frames(buffer, chan_spec, &frames);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = als31300_decoder_get_frame_count,
	.get_size_info = als31300_decoder_get_size_info,
	.decode = als31300_decoder_decode,
};

int als31300_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();
	return 0;
}
