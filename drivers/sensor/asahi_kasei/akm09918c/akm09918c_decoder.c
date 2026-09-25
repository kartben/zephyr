/*
 * Copyright (c) 2023 Google LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/sys/byteorder.h>

#include "akm09918c.h"

#define DT_DRV_COMPAT asahi_kasei_akm09918c

/*
 * Fixed shift value to use. All channels (MAGN_X, _Y, and _Z) have the same fixed range of
 * +/- 49.12 Gauss.
 */
#define AKM09918C_SHIFT 6

static bool akm09918c_chan_is_supported(struct sensor_chan_spec chan_spec)
{
	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_MAGN_X:
	case SENSOR_CHAN_MAGN_Y:
	case SENSOR_CHAN_MAGN_Z:
	case SENSOR_CHAN_MAGN_XYZ:
		return chan_spec.chan_idx == 0U;
	default:
		return false;
	}
}

static int akm09918c_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				  const void *user_data, struct sensor_frame_reading *reading)
{
	uint8_t first;
	uint8_t num;

	ARG_UNUSED(user_data);

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_MAGN_XYZ:
		first = 0U;
		num = 3U;
		break;
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
	default:
		return -ENOTSUP;
	}

	if (reading == NULL) {
		return 1;
	}

	for (uint8_t i = 0U; i < num; i++) {
		reading->values[i] = sensor_raw_to_q31_ratio(sys_get_le16(&frame[(first + i) * 2U]),
							     16U, AKM09918C_MICRO_GAUSS_PER_BIT,
							     1000000, AKM09918C_SHIFT);
	}

	return 1;
}

/* The sensor has no FIFO: the buffer holds a single sample */
static void akm09918c_get_frames(const uint8_t *buffer, struct sensor_raw_frames *frames)
{
	const struct akm09918c_encoded_data *edata = (const struct akm09918c_encoded_data *)buffer;

	*frames = (struct sensor_raw_frames){
		.frames = edata->reading.data,
		.size = sizeof(edata->reading.data),
		.frame_size = sizeof(edata->reading.data),
		.decode_frame = akm09918c_decode_frame,
		.timestamp_ns = edata->header.timestamp,
		.shift = AKM09918C_SHIFT,
	};
}

static int akm09918c_decoder_get_frame_count(const uint8_t *buffer,
					     struct sensor_chan_spec chan_spec,
					     uint16_t *frame_count)
{
	struct sensor_raw_frames frames;

	if (!akm09918c_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	akm09918c_get_frames(buffer, &frames);

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int akm09918c_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					   size_t *frame_size)
{
	if (!akm09918c_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 0U, base_size, frame_size);
}

static int akm09918c_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				    uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_raw_frames frames;

	if (!akm09918c_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	akm09918c_get_frames(buffer, &frames);

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = akm09918c_decoder_get_frame_count,
	.get_size_info = akm09918c_decoder_get_size_info,
	.decode = akm09918c_decoder_decode,
};

int akm09918c_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
