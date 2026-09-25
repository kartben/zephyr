/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_data_types.h>
#include <zephyr/drivers/sensor_decoder.h>

/* The iterator holds the offset of the current frame and the index of the reading in it */
#define FIT_POS_MASK  GENMASK(23, 0)
#define FIT_SUB_SHIFT 24U
#define FIT_MAX_POS   BIT(24)

static int num_values_get(struct sensor_chan_spec chan_spec, uint8_t num_values)
{
	if (num_values != 0U) {
		return (num_values == 1U || num_values == 3U || num_values == 4U) ? num_values
										  : -EINVAL;
	}

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_PROX:
	case SENSOR_CHAN_GAUGE_CYCLE_COUNT:
		return -ENOTSUP;
	case SENSOR_CHAN_GAME_ROTATION_VECTOR:
		return 4;
	case SENSOR_CHAN_GRAVITY_VECTOR:
	case SENSOR_CHAN_GBIAS_XYZ:
		return 3;
	default:
		return SENSOR_CHANNEL_3_AXIS(chan_spec.chan_type) ? 3 : 1;
	}
}

static int frames_check(const struct sensor_raw_frames *frames, struct sensor_chan_spec chan_spec)
{
	__ASSERT_NO_MSG(frames != NULL && frames->decode_frame != NULL);

	if (chan_spec.chan_idx > frames->max_chan_idx) {
		return -ENOTSUP;
	}

	if (frames->size >= FIT_MAX_POS ||
	    (frames->frame_len == NULL && frames->frame_size == 0U)) {
		return -EINVAL;
	}

	return num_values_get(chan_spec, frames->num_values);
}

/* Get the length of the frame at pos, 0 at the end of the frames */
static int frame_len_get(const struct sensor_raw_frames *frames, size_t pos, size_t *len)
{
	size_t remaining = frames->size - pos;
	int rc;

	if (remaining == 0U) {
		return 0;
	}

	if (frames->frame_len == NULL) {
		*len = frames->frame_size;
	} else {
		rc = frames->frame_len(&frames->frames[pos], remaining, frames->user_data);
		if (rc <= 0) {
			return rc;
		}
		*len = (size_t)rc;
	}

	return (*len <= remaining) ? 1 : 0;
}

static int frame_readings_count(const struct sensor_raw_frames *frames, size_t pos,
				struct sensor_chan_spec chan_spec)
{
	int rc = frames->decode_frame(&frames->frames[pos], chan_spec, frames->user_data, NULL);

	return (rc > UINT8_MAX) ? -EINVAL : rc;
}

/*
 * Count the readings of the channel in all frames (total) and before the iterator (before).
 */
static int readings_count(const struct sensor_raw_frames *frames, struct sensor_chan_spec chan_spec,
			  uint32_t fit, uint32_t *total, uint32_t *before)
{
	const size_t fit_pos = fit & FIT_POS_MASK;
	const uint32_t fit_sub = fit >> FIT_SUB_SHIFT;
	size_t pos = 0U;
	size_t len;
	int rc;

	*total = 0U;
	*before = 0U;

	while ((rc = frame_len_get(frames, pos, &len)) > 0) {
		rc = frame_readings_count(frames, pos, chan_spec);
		if (rc < 0) {
			return rc;
		}

		if (pos < fit_pos) {
			*before += (uint32_t)rc;
		} else if (pos == fit_pos) {
			*before += MIN(fit_sub, (uint32_t)rc);
		}

		*total += (uint32_t)rc;
		pos += len;
	}

	return rc;
}

static void reading_slot_get(void *data_out, int num_values, uint16_t idx, uint32_t **delta,
			     q31_t **values)
{
	if (num_values == 4) {
		struct sensor_game_rotation_vector_data *out = data_out;

		*delta = &out->readings[idx].timestamp_delta;
		*values = out->readings[idx].values;
	} else if (num_values == 3) {
		struct sensor_three_axis_data *out = data_out;

		*delta = &out->readings[idx].timestamp_delta;
		*values = out->readings[idx].values;
	} else {
		struct sensor_q31_data *out = data_out;

		*delta = &out->readings[idx].timestamp_delta;
		*values = &out->readings[idx].value;
	}
}

static uint64_t reading_timestamp(const struct sensor_raw_frames *frames, uint32_t total,
				  uint32_t idx)
{
	uint64_t age = (uint64_t)(total - 1U - idx) * frames->period_ns;

	if (frames->period_den != 0U) {
		age /= frames->period_den;
	}

	return frames->timestamp_ns - age;
}

int sensor_decode_frames(const struct sensor_raw_frames *frames, struct sensor_chan_spec chan_spec,
			 uint32_t *fit, uint16_t max_count, void *data_out)
{
	/* All output types share the layout of the header and shift */
	struct sensor_q31_data *out = data_out;
	uint64_t base_ns = 0U;
	uint16_t count = 0U;
	bool rebase = false;
	uint32_t total;
	uint32_t idx;
	size_t pos;
	uint32_t sub;
	size_t len;
	int num_values;
	int rc;

	__ASSERT_NO_MSG(fit != NULL && data_out != NULL);

	num_values = frames_check(frames, chan_spec);
	if (num_values < 0) {
		return num_values;
	}

	rc = readings_count(frames, chan_spec, *fit, &total, &idx);
	if (rc < 0) {
		return rc;
	}

	if (total == 0U && frame_len_get(frames, 0U, &len) > 0) {
		/* The buffer holds data, but not of this channel */
		return -ENODATA;
	}

	if (max_count == 0U || idx >= total) {
		return 0;
	}

	pos = *fit & FIT_POS_MASK;
	sub = *fit >> FIT_SUB_SHIFT;

	while (!rebase && count < max_count) {
		int readings;

		rc = frame_len_get(frames, pos, &len);
		if (rc <= 0) {
			break;
		}

		readings = frame_readings_count(frames, pos, chan_spec);
		if (readings < 0) {
			return readings;
		}

		while (sub < (uint32_t)readings && count < max_count) {
			struct sensor_frame_reading reading = {
				.index = (uint8_t)sub,
				.timestamp_ns = reading_timestamp(frames, total, idx),
			};
			uint32_t *delta;
			int err;

			reading_slot_get(data_out, num_values, count, &delta, &reading.values);
			memset(reading.values, 0, num_values * sizeof(q31_t));

			err = frames->decode_frame(&frames->frames[pos], chan_spec,
						   frames->user_data, &reading);
			if (err < 0 && err != -ENODATA) {
				return err;
			}

			if (err >= 0 && count > 0U &&
			    (reading.timestamp_ns < base_ns ||
			     reading.timestamp_ns - base_ns > UINT32_MAX)) {
				/* Continue from this reading with a new base timestamp */
				rebase = true;
				break;
			}

			if (err >= 0) {
				if (count == 0U) {
					base_ns = reading.timestamp_ns;
				}
				*delta = (uint32_t)(reading.timestamp_ns - base_ns);
				count++;
			}

			idx++;
			sub++;
		}

		if (rebase || sub < (uint32_t)readings) {
			break;
		}

		pos += len;
		sub = 0U;
	}

	if (rc < 0) {
		return rc;
	}

	*fit = (uint32_t)pos | (sub << FIT_SUB_SHIFT);
	out->header.base_timestamp_ns = base_ns;
	out->header.reading_count = count;
	out->shift = frames->shift;

	return count;
}

int sensor_raw_frames_count(const struct sensor_raw_frames *frames,
			    struct sensor_chan_spec chan_spec, uint16_t *frame_count)
{
	uint32_t total;
	uint32_t before;
	int rc;

	rc = frames_check(frames, chan_spec);
	if (rc < 0) {
		return rc;
	}

	rc = readings_count(frames, chan_spec, 0U, &total, &before);
	if (rc < 0) {
		return rc;
	}

	*frame_count = (uint16_t)MIN(total, UINT16_MAX);

	return 0;
}

int sensor_decode_frames_size_info(struct sensor_chan_spec chan_spec, uint8_t num_values,
				   size_t *base_size, size_t *frame_size)
{
	int rc = num_values_get(chan_spec, num_values);

	switch (rc) {
	case 1:
		*base_size = sizeof(struct sensor_q31_data);
		*frame_size = sizeof(struct sensor_q31_sample_data);
		return 0;
	case 3:
		*base_size = sizeof(struct sensor_three_axis_data);
		*frame_size = sizeof(struct sensor_three_axis_sample_data);
		return 0;
	case 4:
		*base_size = sizeof(struct sensor_game_rotation_vector_data);
		*frame_size = sizeof(struct sensor_game_rotation_vector_sample_data);
		return 0;
	default:
		return -ENOTSUP;
	}
}
