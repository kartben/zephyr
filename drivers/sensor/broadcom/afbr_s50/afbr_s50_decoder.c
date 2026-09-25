/*
 * Copyright (c) 2025 Croxel Inc.
 * Copyright (c) 2025 CogniPilot Foundation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT brcm_afbr_s50

#include <zephyr/drivers/sensor_clock.h>
#include <zephyr/drivers/sensor_decoder.h>
#include <api/argus_res.h>
#include <zephyr/drivers/sensor/afbr_s50.h>

#include "afbr_s50_decoder.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(AFBR_S50_DECODER, CONFIG_SENSOR_LOG_LEVEL);

uint8_t afbr_s50_encode_channel(uint16_t chan)
{
	switch (chan) {
	case SENSOR_CHAN_DISTANCE:
		return BIT(0);
	case SENSOR_CHAN_AFBR_S50_PIXELS:
		return BIT(1);
	default:
		return 0;
	}
}

uint8_t afbr_s50_encode_event(enum sensor_trigger_type trigger)
{
	if (trigger == SENSOR_TRIG_DATA_READY) {
		return BIT(0);
	}

	return 0;
}

/* Readings of SENSOR_CHAN_AFBR_S50_PIXELS: the active pixels, without the reference pixel */
#define AFBR_S50_PIXEL_COUNT ARGUS_PIXELS

BUILD_ASSERT(AFBR_S50_PIXEL_COUNT <= UINT8_MAX);

/* Range values are in meters, in Q9.22 format */
#define AFBR_S50_RANGE_SHIFT 9

static int afbr_s50_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				 const void *user_data, struct sensor_frame_reading *reading)
{
	const struct afbr_s50_edata *edata = (const struct afbr_s50_edata *)frame;
	const argus_pixel_t *pixel;
	int readings;

	ARG_UNUSED(user_data);

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_DISTANCE:
		readings = 1;
		break;
	case SENSOR_CHAN_AFBR_S50_PIXELS:
		readings = AFBR_S50_PIXEL_COUNT;
		break;
	default:
		return -ENOTSUP;
	}

	if ((edata->header.channels & afbr_s50_encode_channel(chan_spec.chan_type)) == 0U) {
		return 0;
	}

	if (reading == NULL) {
		return readings;
	}

	if (chan_spec.chan_type == SENSOR_CHAN_DISTANCE) {
		reading->values[0] = edata->payload.Bin.Range;
		return readings;
	}

	/* Invalid pixels keep their reading so that reading n is always pixel n */
	pixel = &edata->payload.Pixels[reading->index];
	if (pixel->Amplitude == 0xFFFFU || pixel->Status != PIXEL_OK) {
		LOG_DBG("Invalid pixel: %u, Amplitude: %u, Status: %u", reading->index,
			pixel->Amplitude, pixel->Status);
		reading->values[0] = (q31_t)AFBR_PIXEL_INVALID_VALUE;
	} else {
		reading->values[0] = pixel->Range;
	}

	return readings;
}

static void afbr_s50_get_frames(const uint8_t *buffer, struct sensor_raw_frames *frames)
{
	const struct afbr_s50_edata *edata = (const struct afbr_s50_edata *)buffer;

	/* The buffer holds a single measurement: one frame for all the channels */
	*frames = (struct sensor_raw_frames){
		.frames = buffer,
		.size = sizeof(struct afbr_s50_edata),
		.frame_size = sizeof(struct afbr_s50_edata),
		.decode_frame = afbr_s50_decode_frame,
		.timestamp_ns = edata->header.timestamp,
		.shift = AFBR_S50_RANGE_SHIFT,
		.num_values = 1U,
	};
}

static int afbr_s50_decoder_get_frame_count(const uint8_t *buffer,
					    struct sensor_chan_spec chan_spec,
					    uint16_t *frame_count)
{
	struct sensor_raw_frames frames;

	afbr_s50_get_frames(buffer, &frames);

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int afbr_s50_decoder_get_size_info(struct sensor_chan_spec chan_spec,
					  size_t *base_size,
					  size_t *frame_size)
{
	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_DISTANCE:
	case SENSOR_CHAN_AFBR_S50_PIXELS:
		return sensor_decode_frames_size_info(chan_spec, 1U, base_size, frame_size);
	default:
		return -ENOTSUP;
	}
}

static int afbr_s50_decoder_decode(const uint8_t *buffer,
				   struct sensor_chan_spec chan_spec,
				   uint32_t *fit,
				   uint16_t max_count,
				   void *data_out)
{
	struct sensor_raw_frames frames;

	afbr_s50_get_frames(buffer, &frames);

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

static bool afbr_s50_decoder_has_trigger(const uint8_t *buffer,
					 enum sensor_trigger_type trigger)
{
	const struct afbr_s50_edata *edata = (const struct afbr_s50_edata *)buffer;

	if (trigger == SENSOR_TRIG_DATA_READY) {
		return edata->header.events & afbr_s50_encode_event(SENSOR_TRIG_DATA_READY);
	} else {
		return false;
	}
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = afbr_s50_decoder_get_frame_count,
	.get_size_info = afbr_s50_decoder_get_size_info,
	.decode = afbr_s50_decoder_decode,
	.has_trigger = afbr_s50_decoder_has_trigger,
};

int afbr_s50_get_decoder(const struct device *dev,
			 const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
