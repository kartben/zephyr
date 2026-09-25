/*
 * Copyright (c) 2022 Espressif Systems (Shanghai) Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT espressif_esp32_pcnt

#include <errno.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/sys/util.h>

#include "pcnt_esp32_decoder.h"

/* Angles in [0, 360) degrees fit a shift of 9 */
#define PCNT_ESP32_ROTATION_SHIFT 9

static bool pcnt_esp32_chan_is_supported(struct sensor_chan_spec chan_spec)
{
	return chan_spec.chan_type == SENSOR_CHAN_ROTATION ||
	       chan_spec.chan_type == SENSOR_CHAN_ENCODER_COUNT;
}

/*
 * SENSOR_CHAN_ROTATION is an angle for units with counts-per-revolution, and the raw count
 * like SENSOR_CHAN_ENCODER_COUNT otherwise.
 */
static bool pcnt_esp32_is_angle(struct sensor_chan_spec chan_spec, uint32_t counts_per_rev)
{
	return chan_spec.chan_type == SENSOR_CHAN_ROTATION && counts_per_rev != 0U;
}

static int pcnt_esp32_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				   const void *user_data, struct sensor_frame_reading *reading)
{
	const struct pcnt_esp32_encoded_data *edata = (const struct pcnt_esp32_encoded_data *)frame;
	const uint8_t unit = *(const uint8_t *)user_data;
	const int32_t count = edata->counts[unit];
	const uint32_t counts_per_rev = edata->counts_per_rev[unit];
	int64_t pos;

	if (reading == NULL) {
		return 1;
	}

	if (pcnt_esp32_is_angle(chan_spec, counts_per_rev)) {
		/* Position within the revolution, in [0, counts_per_rev) */
		pos = (int64_t)count % (int64_t)counts_per_rev;
		if (pos < 0) {
			pos += counts_per_rev;
		}

		reading->values[0] =
			sensor_raw_to_q31_ratio((uint32_t)pos, 32U, 360, (int32_t)counts_per_rev,
						PCNT_ESP32_ROTATION_SHIFT);
	} else {
		reading->values[0] = count;
	}

	return 1;
}

/* Describe the sample of the unit selected by chan_spec.chan_idx; unit must outlive frames */
static int pcnt_esp32_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				 uint8_t *unit, struct sensor_raw_frames *frames)
{
	const struct pcnt_esp32_encoded_data *edata =
		(const struct pcnt_esp32_encoded_data *)buffer;
	const uint8_t num_units = MIN(edata->num_units, PCNT_ESP32_MAX_UNITS);
	uint32_t counts_per_rev;
	uint8_t i;

	if (!pcnt_esp32_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	for (i = 0U; i < num_units; i++) {
		if (edata->unit_idx[i] == chan_spec.chan_idx) {
			break;
		}
	}

	if (i == num_units) {
		return -EINVAL;
	}

	/* The angle conversion takes a signed 32-bit number of counts per revolution */
	counts_per_rev = edata->counts_per_rev[i];
	if (pcnt_esp32_is_angle(chan_spec, counts_per_rev) &&
	    counts_per_rev > (uint32_t)INT32_MAX) {
		return -EINVAL;
	}

	*unit = i;
	*frames = (struct sensor_raw_frames){
		.frames = buffer,
		.size = sizeof(*edata),
		.frame_size = sizeof(*edata),
		.decode_frame = pcnt_esp32_decode_frame,
		.user_data = unit,
		.timestamp_ns = edata->timestamp_ns,
		.shift = pcnt_esp32_is_angle(chan_spec, counts_per_rev) ? PCNT_ESP32_ROTATION_SHIFT
									: 0,
		.num_values = 1U,
		.max_chan_idx = UINT8_MAX,
	};

	return 0;
}

static int pcnt_esp32_decoder_get_frame_count(const uint8_t *buffer,
					      struct sensor_chan_spec chan_spec,
					      uint16_t *frame_count)
{
	struct sensor_raw_frames frames;
	uint8_t unit;
	int rc;

	rc = pcnt_esp32_get_frames(buffer, chan_spec, &unit, &frames);
	if (rc != 0) {
		return rc;
	}

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int pcnt_esp32_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					    size_t *frame_size)
{
	if (!pcnt_esp32_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 1U, base_size, frame_size);
}

static int pcnt_esp32_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				     uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_raw_frames frames;
	uint8_t unit;
	int rc;

	rc = pcnt_esp32_get_frames(buffer, chan_spec, &unit, &frames);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = pcnt_esp32_decoder_get_frame_count,
	.get_size_info = pcnt_esp32_decoder_get_size_info,
	.decode = pcnt_esp32_decoder_decode,
};

int pcnt_esp32_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
