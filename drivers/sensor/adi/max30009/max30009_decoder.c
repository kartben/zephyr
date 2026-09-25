/*
 * Copyright (c) 2026 Analog Devices Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "max30009.h"
#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(MAX30009);

/* FIFO sample: TAG[23:20] followed by 20-bit 2's complement DATA[19:0] (datasheet Table 4) */
#define MAX30009_FIFO_TAG_MASK  GENMASK(23, 20)
#define MAX30009_FIFO_DATA_BITS 20U
#define MAX30009_BIOZ_I_TAG     0x1U
#define MAX30009_BIOZ_Q_TAG     0x2U

static bool max30009_chan_is_supported(struct sensor_chan_spec chan_spec)
{
	return chan_spec.chan_idx == 0U && ((int)chan_spec.chan_type == SENSOR_CHAN_BIOZ_I ||
					    (int)chan_spec.chan_type == SENSOR_CHAN_BIOZ_Q);
}

/*
 * Each FIFO word carries one BioZ I or Q sample. Words of the other channel, and markers,
 * hold no reading for the requested channel. Samples are raw ADC counts with a shift of 0.
 */
static int max30009_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				 const void *user_data, struct sensor_frame_reading *reading)
{
	const uint32_t word = sys_get_be24(frame);
	const uint32_t tag = FIELD_GET(MAX30009_FIFO_TAG_MASK, word);

	ARG_UNUSED(user_data);

	switch ((int)chan_spec.chan_type) {
	case SENSOR_CHAN_BIOZ_I:
		if (tag != MAX30009_BIOZ_I_TAG) {
			return 0;
		}
		break;
	case SENSOR_CHAN_BIOZ_Q:
		if (tag != MAX30009_BIOZ_Q_TAG) {
			return 0;
		}
		break;
	default:
		return -ENOTSUP;
	}

	if (reading != NULL) {
		reading->values[0] = sensor_raw_to_q31(word, MAX30009_FIFO_DATA_BITS, 1);
	}

	return 1;
}

static void max30009_get_frames(const uint8_t *buffer, struct sensor_raw_frames *frames)
{
	const struct max30009_fifo_data *hdr = (const struct max30009_fifo_data *)buffer;

	*frames = (struct sensor_raw_frames){
		.frames = buffer + sizeof(*hdr),
		.size = hdr->fifo_byte_count,
		.frame_size = MAX30009_FIFO_BYTES_PER_SAMPLE,
		.decode_frame = max30009_decode_frame,
		.timestamp_ns = hdr->timestamp,
		.period_ns = hdr->sample_period_ns,
		.shift = 0,
		.num_values = 1U,
	};
}

static int max30009_decoder_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec channel,
					    uint16_t *frame_count)
{
	struct sensor_raw_frames frames;

	if (!max30009_chan_is_supported(channel)) {
		return -ENOTSUP;
	}

	max30009_get_frames(buffer, &frames);

	return sensor_raw_frames_count(&frames, channel, frame_count);
}

static int max30009_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec channel,
				   uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_raw_frames frames;

	if (!max30009_chan_is_supported(channel)) {
		LOG_ERR("Unsupported channel %d index %u", channel.chan_type, channel.chan_idx);
		return -ENOTSUP;
	}

	max30009_get_frames(buffer, &frames);

	return sensor_decode_frames(&frames, channel, fit, max_count, data_out);
}

static int max30009_decoder_get_size_info(struct sensor_chan_spec channel, size_t *base_size,
					  size_t *frame_size)
{
	__ASSERT_NO_MSG(base_size != NULL);
	__ASSERT_NO_MSG(frame_size != NULL);

	if (!max30009_chan_is_supported(channel)) {
		LOG_ERR("Unsupported channel %d index %u", channel.chan_type, channel.chan_idx);
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(channel, 1U, base_size, frame_size);
}

static bool max30009_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	const struct max30009_fifo_data *fifo_data = (const struct max30009_fifo_data *)buffer;

	switch (trigger) {
	case SENSOR_TRIG_FIFO_FULL:
	case SENSOR_TRIG_FIFO_WATERMARK:
		return FIELD_GET(MAX30009_STATUS1_A_FULL_MSK, fifo_data->status1);
	default:
		return false;
	}
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = max30009_decoder_get_frame_count,
	.get_size_info = max30009_decoder_get_size_info,
	.decode = max30009_decoder_decode,
	.has_trigger = max30009_decoder_has_trigger,
};

/**
 * @brief Get the sensor decoder API for MAX30009
 *
 * @param dev Device pointer
 * @param decoder Decoder API pointer
 * @return int 0 on success, negative error code otherwise
 */
int max30009_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();
	return 0;
}
