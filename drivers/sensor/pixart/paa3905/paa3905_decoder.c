/*
 * Copyright (c) 2025 Croxel Inc.
 * Copyright (c) 2025 CogniPilot Foundation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor_clock.h>
#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/sys/byteorder.h>

#include "paa3905.h"
#include "paa3905_reg.h"
#include "paa3905_decoder.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(PAA3905_DECODER, CONFIG_SENSOR_LOG_LEVEL);

#define DT_DRV_COMPAT pixart_paa3905

/* Delta counts are reported unscaled, one count per LSB */
#define PAA3905_Q31_SHIFT 31
#define PAA3905_Q31_SCALE SENSOR_Q31_SCALE(1, 1, PAA3905_Q31_SHIFT)

uint8_t paa3905_encode_channel(enum sensor_channel chan)
{
	switch (chan) {
	case SENSOR_CHAN_POS_DX:
		return BIT(0);
	case SENSOR_CHAN_POS_DY:
		return BIT(1);
	case SENSOR_CHAN_POS_DXYZ:
		return BIT(2);
	default:
		return 0;
	}
}

static bool is_data_valid(const uint8_t *burst)
{
	const uint8_t motion = burst[BURST_MOTION];
	const uint8_t observation = burst[BURST_OBSERVATION];
	const uint8_t squal = burst[BURST_SQUAL];
	uint8_t squal_min;
	uint32_t shutter_max;
	uint32_t shutter;

	if (REG_MOTION_DETECTED(motion) == 0U) {
		LOG_DBG("Invalid data - No motion detected");
		return false;
	}

	if (REG_MOTION_CHALLENGING_COND(motion) != 0U) {
		LOG_WRN("Invalid data - Challenging conditions");
		return false;
	}

	switch (REG_OBSERVATION_MODE(observation)) {
	case OBSERVATION_MODE_BRIGHT:
		squal_min = SQUAL_MIN_BRIGHT;
		shutter_max = SHUTTER_MAX_BRIGHT;
		break;
	case OBSERVATION_MODE_LOW_LIGHT:
		squal_min = SQUAL_MIN_LOW_LIGHT;
		shutter_max = SHUTTER_MAX_LOW_LIGHT;
		break;
	case OBSERVATION_MODE_SUPER_LOW_LIGHT:
		squal_min = SQUAL_MIN_SUPER_LOW_LIGHT;
		shutter_max = SHUTTER_MAX_SUPER_LOW_LIGHT;
		break;
	default:
		LOG_ERR("Invalid op mode");
		return false;
	}

	shutter = sys_get_be24(&burst[BURST_SHUTTER]);

	if (squal < squal_min || shutter >= shutter_max) {
		LOG_WRN("Invalid data - mode: %d squal: 0x%02X shutter: 0x%06X",
			(uint8_t)REG_OBSERVATION_MODE(observation), squal, shutter);

		return false;
	}

	return true;
}

static bool paa3905_chan_is_supported(struct sensor_chan_spec chan_spec)
{
	return chan_spec.chan_idx == 0U && paa3905_encode_channel(chan_spec.chan_type) != 0U;
}

static int paa3905_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				const void *user_data, struct sensor_frame_reading *reading)
{
	const struct paa3905_encoded_data *edata = user_data;
	const uint8_t channel_request = paa3905_encode_channel(chan_spec.chan_type);
	q31_t dx;
	q31_t dy;

	if (channel_request == 0U) {
		return -ENOTSUP;
	}

	if ((edata->header.channels & channel_request) == 0U || !is_data_valid(frame)) {
		return -ENODATA;
	}

	if (reading == NULL) {
		return 1;
	}

	dx = sensor_raw_to_q31(sys_get_le16(&frame[BURST_DELTA_X]), 16U, PAA3905_Q31_SCALE);
	dy = sensor_raw_to_q31(sys_get_le16(&frame[BURST_DELTA_Y]), 16U, PAA3905_Q31_SCALE);

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_POS_DX:
		reading->values[0] = dx;
		break;
	case SENSOR_CHAN_POS_DY:
		reading->values[0] = dy;
		break;
	default:
		/* SENSOR_CHAN_POS_DXYZ, z stays 0 */
		reading->values[0] = dx;
		reading->values[1] = dy;
		break;
	}

	return 1;
}

static void paa3905_get_frames(const uint8_t *buffer, struct sensor_raw_frames *frames)
{
	const struct paa3905_encoded_data *edata = (const struct paa3905_encoded_data *)buffer;

	*frames = (struct sensor_raw_frames){
		.frames = edata->buf,
		.size = sizeof(edata->buf),
		.frame_size = sizeof(edata->buf),
		.decode_frame = paa3905_decode_frame,
		.user_data = edata,
		.timestamp_ns = edata->header.timestamp,
		.shift = PAA3905_Q31_SHIFT,
	};
}

static int paa3905_decoder_get_frame_count(const uint8_t *buffer,
					   struct sensor_chan_spec chan_spec,
					   uint16_t *frame_count)
{
	struct sensor_raw_frames frames;

	if (!paa3905_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	paa3905_get_frames(buffer, &frames);

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int paa3905_decoder_get_size_info(struct sensor_chan_spec chan_spec,
					 size_t *base_size,
					 size_t *frame_size)
{
	if (!paa3905_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 0U, base_size, frame_size);
}

static int paa3905_decoder_decode(const uint8_t *buffer,
				  struct sensor_chan_spec chan_spec,
				  uint32_t *fit,
				  uint16_t max_count,
				  void *data_out)
{
	struct sensor_raw_frames frames;

	if (!paa3905_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	paa3905_get_frames(buffer, &frames);

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

static bool paa3905_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	struct paa3905_encoded_data *edata = (struct paa3905_encoded_data *)buffer;

	switch (trigger) {
	case SENSOR_TRIG_DATA_READY:
		return edata->header.events.drdy;
	case SENSOR_TRIG_MOTION:
		return edata->header.events.motion;
	default:
		return false;
	}
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = paa3905_decoder_get_frame_count,
	.get_size_info = paa3905_decoder_get_size_info,
	.decode = paa3905_decoder_decode,
	.has_trigger = paa3905_decoder_has_trigger,
};

int paa3905_get_decoder(const struct device *dev,
	const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}

int paa3905_encode(const struct device *dev,
		   const struct sensor_chan_spec *const channels,
		   size_t num_channels,
		   uint8_t *buf)
{
	struct paa3905_encoded_data *edata = (struct paa3905_encoded_data *)buf;
	uint64_t cycles;
	int err;

	edata->header.channels = 0;
	edata->header.events.drdy = false;
	edata->header.events.motion = false;

	for (size_t i = 0 ; i < num_channels; i++) {
		edata->header.channels |= paa3905_encode_channel(channels[i].chan_type);
	}

	err = sensor_clock_get_cycles(&cycles);
	if (err != 0) {
		return err;
	}

	edata->header.timestamp = sensor_clock_cycles_to_ns(cycles);

	return 0;
}
