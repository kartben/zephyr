/*
 * Copyright (c) 2025 Croxel Inc.
 * Copyright (c) 2025 CogniPilot Foundation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor_clock.h>
#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/sensor/pat9136.h>

#include "pat9136.h"
#include "pat9136_reg.h"
#include "pat9136_decoder.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(PAT9136_DECODER, CONFIG_SENSOR_LOG_LEVEL);

#define DT_DRV_COMPAT pixart_pat9136

/* Offsets of the little-endian X and Y motion deltas in the burst read data */
#define PAT9136_DELTA_X_OFFSET 2U
#define PAT9136_DELTA_Y_OFFSET 4U

BUILD_ASSERT(offsetof(struct pat9136_encoded_data, delta.x) -
		     offsetof(struct pat9136_encoded_data, buf) ==
	     PAT9136_DELTA_X_OFFSET);
BUILD_ASSERT(offsetof(struct pat9136_encoded_data, delta.y) -
		     offsetof(struct pat9136_encoded_data, buf) ==
	     PAT9136_DELTA_Y_OFFSET);

/* Magnitude of the largest motion delta, in counts */
#define PAT9136_DELTA_MAX 32768U

/* One inch is 254 / 10 mm */
#define PAT9136_MM_PER_INCH_NUM 254
#define PAT9136_MM_PER_INCH_DEN 10

struct pat9136_chan_info {
	/* First axis read from the data: 0 for X, 1 for Y */
	uint8_t first;
	/* Number of axes read from the data; the Z value of 3-axis channels is 0 */
	uint8_t axes;
	/* Number of values per reading */
	uint8_t num_values;
	/* Output in mm instead of counts */
	bool mm;
};

struct pat9136_frame_format {
	struct pat9136_chan_info chan;
	/* Counts per inch of the X and Y axes */
	int32_t cpi[2];
	int8_t shift;
};

static int pat9136_chan_info_get(uint16_t chan_type, struct pat9136_chan_info *info)
{
	switch (chan_type) {
	case SENSOR_CHAN_POS_DX:
		*info = (struct pat9136_chan_info){.first = 0U, .axes = 1U, .num_values = 1U};
		return 0;
	case SENSOR_CHAN_POS_DY:
		*info = (struct pat9136_chan_info){.first = 1U, .axes = 1U, .num_values = 1U};
		return 0;
	case SENSOR_CHAN_POS_DXYZ:
		*info = (struct pat9136_chan_info){.first = 0U, .axes = 2U, .num_values = 3U};
		return 0;
	case SENSOR_CHAN_POS_DX_MM:
		*info = (struct pat9136_chan_info){
			.first = 0U, .axes = 1U, .num_values = 1U, .mm = true};
		return 0;
	case SENSOR_CHAN_POS_DY_MM:
		*info = (struct pat9136_chan_info){
			.first = 1U, .axes = 1U, .num_values = 1U, .mm = true};
		return 0;
	case SENSOR_CHAN_POS_DXYZ_MM:
		*info = (struct pat9136_chan_info){
			.first = 0U, .axes = 2U, .num_values = 3U, .mm = true};
		return 0;
	default:
		return -ENOTSUP;
	}
}

/*
 * Smallest shift covering the largest delta at the lowest resolution of both axes:
 * 2^shift >= PAT9136_DELTA_MAX * 25.4 / cpi.
 */
static int8_t pat9136_mm_shift(int32_t cpi)
{
	int8_t shift = 0;

	while ((((uint64_t)cpi * PAT9136_MM_PER_INCH_DEN) << shift) <
	       (uint64_t)PAT9136_DELTA_MAX * PAT9136_MM_PER_INCH_NUM) {
		shift++;
	}

	return shift;
}

uint8_t pat9136_encode_channel(uint16_t chan)
{
	switch (chan) {
	case SENSOR_CHAN_POS_DX:
		return BIT(0);
	case SENSOR_CHAN_POS_DY:
		return BIT(1);
	case SENSOR_CHAN_POS_DXYZ:
		return BIT(2);
	case SENSOR_CHAN_POS_DX_MM:
		return BIT(3);
	case SENSOR_CHAN_POS_DY_MM:
		return BIT(4);
	case SENSOR_CHAN_POS_DXYZ_MM:
		return BIT(5);
	case SENSOR_CHAN_ALL:
		return BIT_MASK(6);
	default:
		return 0;
	}
}

static bool is_data_valid(const struct pat9136_encoded_data *edata)
{
	if (!REG_MOTION_DETECTED(edata->motion)) {
		LOG_DBG("Invalid data - No motion detected");
		return false;
	}

	if (!REG_OBSERVATION_READ_IS_VALID(edata->observation)) {
		LOG_DBG("Invalid data - Observation read is not valid");
		return false;
	}

	return true;
}

static int pat9136_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				const void *user_data, struct sensor_frame_reading *reading)
{
	static const uint8_t offset[] = {PAT9136_DELTA_X_OFFSET, PAT9136_DELTA_Y_OFFSET};
	const struct pat9136_frame_format *fmt = user_data;

	ARG_UNUSED(chan_spec);

	if (reading == NULL) {
		return 1;
	}

	for (uint8_t i = 0U; i < fmt->chan.axes; i++) {
		uint8_t axis = fmt->chan.first + i;
		uint32_t raw = sys_get_le16(&frame[offset[axis]]);

		if (fmt->chan.mm) {
			reading->values[i] = sensor_raw_to_q31_ratio(
				raw, 16U, PAT9136_MM_PER_INCH_NUM,
				fmt->cpi[axis] * PAT9136_MM_PER_INCH_DEN, fmt->shift);
		} else {
			reading->values[i] = sensor_raw_to_q31(raw, 16U, 1);
		}
	}

	return 1;
}

static int pat9136_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
			      struct sensor_raw_frames *frames, struct pat9136_frame_format *fmt)
{
	const struct pat9136_encoded_data *edata = (const struct pat9136_encoded_data *)buffer;
	uint8_t channel_request;
	int rc;

	rc = pat9136_chan_info_get(chan_spec.chan_type, &fmt->chan);
	if (rc != 0) {
		return rc;
	}

	if (chan_spec.chan_idx != 0U) {
		return -ENOTSUP;
	}

	channel_request = pat9136_encode_channel(chan_spec.chan_type);
	if (((edata->header.channels & channel_request) != channel_request) ||
	    !is_data_valid(edata)) {
		return -ENODATA;
	}

	/* CPI = (resolution + 1) * 100 */
	fmt->cpi[0] = ((int32_t)sys_get_le16(&edata->header.resolution.buf[0]) + 1) * 100;
	fmt->cpi[1] = ((int32_t)sys_get_le16(&edata->header.resolution.buf[2]) + 1) * 100;
	fmt->shift = fmt->chan.mm ? pat9136_mm_shift(MIN(fmt->cpi[0], fmt->cpi[1])) : 31;

	*frames = (struct sensor_raw_frames){
		.frames = edata->buf,
		.size = sizeof(edata->buf),
		.frame_size = sizeof(edata->buf),
		.decode_frame = pat9136_decode_frame,
		.user_data = fmt,
		.timestamp_ns = edata->header.timestamp,
		.shift = fmt->shift,
		.num_values = fmt->chan.num_values,
	};

	return 0;
}

static int pat9136_decoder_get_frame_count(const uint8_t *buffer,
					   struct sensor_chan_spec chan_spec,
					   uint16_t *frame_count)
{
	struct sensor_raw_frames frames;
	struct pat9136_frame_format fmt;
	int rc;

	rc = pat9136_get_frames(buffer, chan_spec, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int pat9136_decoder_get_size_info(struct sensor_chan_spec chan_spec,
					 size_t *base_size,
					 size_t *frame_size)
{
	struct pat9136_chan_info info;
	int rc;

	rc = pat9136_chan_info_get(chan_spec.chan_type, &info);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames_size_info(chan_spec, info.num_values, base_size, frame_size);
}

static int pat9136_decoder_decode(const uint8_t *buffer,
				  struct sensor_chan_spec chan_spec,
				  uint32_t *fit,
				  uint16_t max_count,
				  void *data_out)
{
	struct sensor_raw_frames frames;
	struct pat9136_frame_format fmt;
	int rc;

	rc = pat9136_get_frames(buffer, chan_spec, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

static bool pat9136_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	const struct pat9136_encoded_data *edata = (const struct pat9136_encoded_data *)buffer;

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
	.get_frame_count = pat9136_decoder_get_frame_count,
	.get_size_info = pat9136_decoder_get_size_info,
	.decode = pat9136_decoder_decode,
	.has_trigger = pat9136_decoder_has_trigger,
};

int pat9136_get_decoder(const struct device *dev,
	const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}

int pat9136_encode(const struct device *dev,
		   const struct sensor_chan_spec *const channels,
		   size_t num_channels,
		   uint8_t *buf)
{
	struct pat9136_encoded_data *edata = (struct pat9136_encoded_data *)buf;
	uint64_t cycles;
	int err;

	edata->header.channels = 0;
	edata->header.events.drdy = false;
	edata->header.events.motion = false;

	for (size_t i = 0 ; i < num_channels; i++) {
		edata->header.channels |= pat9136_encode_channel(channels[i].chan_type);
	}

	err = sensor_clock_get_cycles(&cycles);
	if (err != 0) {
		return err;
	}

	edata->header.timestamp = sensor_clock_cycles_to_ns(cycles);

	return 0;
}
