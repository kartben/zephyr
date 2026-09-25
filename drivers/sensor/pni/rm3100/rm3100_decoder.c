/*
 * Copyright (c) 2025 Croxel Inc.
 * Copyright (c) 2025 CogniPilot Foundation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT pni_rm3100

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_clock.h>
#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/dt-bindings/sensor/rm3100.h>
#include "rm3100.h"

uint8_t rm3100_encode_channel(enum sensor_channel chan)
{
	switch (chan) {
	case SENSOR_CHAN_MAGN_X:
		return BIT(0);
	case SENSOR_CHAN_MAGN_Y:
		return BIT(1);
	case SENSOR_CHAN_MAGN_Z:
		return BIT(2);
	case SENSOR_CHAN_ALL:
	case SENSOR_CHAN_MAGN_XYZ:
		return BIT(0) | BIT(1) | BIT(2);
	default:
		return 0;
	}
}

int rm3100_encode(const struct device *dev,
		  const struct sensor_chan_spec *const channels,
		  size_t num_channels,
		  uint8_t *buf)
{
	const struct rm3100_data *data = dev->data;
	struct rm3100_encoded_data *edata = (struct rm3100_encoded_data *)buf;
	uint64_t cycles;
	int err;

	edata->header.channels = 0;
	edata->header.status = 0U;
	edata->header.events.drdy = false;

	if (data->settings.odr == RM3100_DT_ODR_600) {
		edata->header.cycle_count = RM3100_CYCLE_COUNT_HIGH_ODR;
	} else {
		edata->header.cycle_count = RM3100_CYCLE_COUNT_DEFAULT;
	}

	for (size_t i = 0; i < num_channels; i++) {
		edata->header.channels |= rm3100_encode_channel(channels[i].chan_type);
	}

	err = sensor_clock_get_cycles(&cycles);
	if (err != 0) {
		return err;
	}

	edata->header.timestamp = sensor_clock_cycles_to_ns(cycles);

	return 0;
}

/* Sensitivity in LSB/Gauss and q31 shift covering the full 24-bit range */
struct rm3100_scale {
	int32_t lsb_per_gauss;
	int8_t shift;
};

/* 75 LSB/uT at the default cycle count (200): 2^23 LSB is below 2^11 Gauss */
static const struct rm3100_scale rm3100_scale_default = {
	.lsb_per_gauss = 7500,
	.shift = 11,
};

/* 38 LSB/uT at the cycle count of the 600 Hz ODR (100): 2^23 LSB is below 2^12 Gauss */
static const struct rm3100_scale rm3100_scale_high_odr = {
	.lsb_per_gauss = 3800,
	.shift = 12,
};

static bool rm3100_chan_is_supported(struct sensor_chan_spec chan_spec)
{
	if (chan_spec.chan_idx != 0U) {
		return false;
	}

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_MAGN_X:
	case SENSOR_CHAN_MAGN_Y:
	case SENSOR_CHAN_MAGN_Z:
	case SENSOR_CHAN_MAGN_XYZ:
		return true;
	default:
		return false;
	}
}

static int rm3100_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
			       const void *user_data, struct sensor_frame_reading *reading)
{
	const struct rm3100_scale *scale = user_data;
	uint8_t first;
	uint8_t num;

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

	/* Each axis is a 24-bit big-endian two's complement value */
	for (uint8_t i = 0U; i < num; i++) {
		reading->values[i] = sensor_raw_to_q31_ratio(
			sys_get_be24(&frame[(first + i) * RM3100_BYTES_PER_AXIS]), 24U, 1,
			scale->lsb_per_gauss, scale->shift);
	}

	return 1;
}

static int rm3100_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
			     struct sensor_raw_frames *frames)
{
	const struct rm3100_encoded_data *edata = (const struct rm3100_encoded_data *)buffer;
	const struct rm3100_scale *scale;
	uint8_t channel_request;

	if (!rm3100_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	channel_request = rm3100_encode_channel(chan_spec.chan_type);
	if ((edata->header.channels & channel_request) != channel_request) {
		return -ENODATA;
	}

	if (edata->header.cycle_count == RM3100_CYCLE_COUNT_DEFAULT) {
		scale = &rm3100_scale_default;
	} else {
		scale = &rm3100_scale_high_odr;
	}

	*frames = (struct sensor_raw_frames){
		.frames = edata->payload,
		.size = sizeof(edata->payload),
		.frame_size = RM3100_TOTAL_BYTES,
		.decode_frame = rm3100_decode_frame,
		.user_data = scale,
		.timestamp_ns = edata->header.timestamp,
		.shift = scale->shift,
	};

	return 0;
}

static int rm3100_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					size_t *frame_size)
{
	if (!rm3100_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 0U, base_size, frame_size);
}

static int rm3100_decoder_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
					  uint16_t *frame_count)
{
	struct sensor_raw_frames frames;
	int rc;

	rc = rm3100_get_frames(buffer, chan_spec, &frames);
	if (rc != 0) {
		return rc;
	}

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int rm3100_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				 uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_raw_frames frames;
	int rc;

	rc = rm3100_get_frames(buffer, chan_spec, &frames);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

static bool rm3100_decoder_has_trigger(const uint8_t *buffer,
					enum sensor_trigger_type trigger)
{
	const struct rm3100_encoded_data *edata = (const struct rm3100_encoded_data *)buffer;

	return edata->header.events.drdy && trigger == SENSOR_TRIG_DATA_READY;
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = rm3100_decoder_get_frame_count,
	.get_size_info = rm3100_decoder_get_size_info,
	.decode = rm3100_decoder_decode,
	.has_trigger = rm3100_decoder_has_trigger,
};

int rm3100_get_decoder(const struct device *dev,
	const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);

	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
