/*
 * Copyright (c) 2024 Florian Weber <Florian.Weber@live.de>
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mlx90394.h"
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#define DT_DRV_COMPAT melexis_mlx90394

/* Index of each value in mlx90394_encoded_data.readings, in 16-bit words */
#define MLX90394_READING_X    0U
#define MLX90394_READING_Y    1U
#define MLX90394_READING_Z    2U
#define MLX90394_READING_TEMP 3U
#define MLX90394_NUM_READINGS 4U

/* Micro-units per unit of the decoded values: gauss and degrees Celsius */
#define MLX90394_MICRO 1000000

struct mlx90394_frame_format {
	/* Channel measured when the buffer was encoded */
	uint16_t measured;
	/* Value of one LSB in micro-units */
	int32_t micro_per_lsb;
	int8_t shift;
};

/* Bit mask of the readings a decoded channel is made of, 0 for other channels */
static uint8_t mlx90394_channel_readings(uint16_t chan)
{
	switch (chan) {
	case SENSOR_CHAN_MAGN_X:
		return BIT(MLX90394_READING_X);
	case SENSOR_CHAN_MAGN_Y:
		return BIT(MLX90394_READING_Y);
	case SENSOR_CHAN_MAGN_Z:
		return BIT(MLX90394_READING_Z);
	case SENSOR_CHAN_MAGN_XYZ:
		return BIT(MLX90394_READING_X) | BIT(MLX90394_READING_Y) | BIT(MLX90394_READING_Z);
	case SENSOR_CHAN_AMBIENT_TEMP:
		return BIT(MLX90394_READING_TEMP);
	default:
		return 0U;
	}
}

/*
 * The sensor only converts the axes and temperature of the measured channel: check that the
 * readings of chan are among them
 */
static bool mlx90394_channel_measured(uint16_t measured, uint16_t chan)
{
	const uint8_t needed = mlx90394_channel_readings(chan);
	uint8_t available;

	if (measured == SENSOR_CHAN_ALL) {
		available = BIT(MLX90394_READING_X) | BIT(MLX90394_READING_Y) |
			    BIT(MLX90394_READING_Z) | BIT(MLX90394_READING_TEMP);
	} else {
		available = mlx90394_channel_readings(measured);
	}

	return (available & needed) == needed;
}

static int mlx90394_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				 const void *user_data, struct sensor_frame_reading *reading)
{
	const struct mlx90394_frame_format *fmt = user_data;
	const uint8_t needed = mlx90394_channel_readings(chan_spec.chan_type);
	uint8_t num = 0U;

	if (needed == 0U) {
		return -ENOTSUP;
	}

	if (!mlx90394_channel_measured(fmt->measured, chan_spec.chan_type)) {
		return 0;
	}

	if (reading == NULL) {
		return 1;
	}

	for (uint8_t i = 0U; i < MLX90394_NUM_READINGS; i++) {
		if ((needed & BIT(i)) != 0U) {
			reading->values[num] = sensor_raw_to_q31_ratio(
				sys_get_le16(&frame[i * sizeof(uint16_t)]), 16U, fmt->micro_per_lsb,
				MLX90394_MICRO, fmt->shift);
			num++;
		}
	}

	return 1;
}

static int mlx90394_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
			       struct sensor_raw_frames *frames, struct mlx90394_frame_format *fmt)
{
	const struct mlx90394_encoded_data *edata = (const struct mlx90394_encoded_data *)buffer;

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_MAGN_X:
	case SENSOR_CHAN_MAGN_Y:
	case SENSOR_CHAN_MAGN_Z:
	case SENSOR_CHAN_MAGN_XYZ:
		if (edata->header.config_val == MLX90394_CTRL2_CONFIG_HIGH_SENSITIVITY_LOW_NOISE) {
			fmt->micro_per_lsb = (int32_t)MLX90394_HIGH_SENSITIVITY_MICRO_GAUSS_PER_BIT;
			fmt->shift = MLX90394_SHIFT_MAGN_HIGH_SENSITIVITY;
		} else {
			fmt->micro_per_lsb = (int32_t)MLX90394_HIGH_RANGE_MICRO_GAUSS_PER_BIT;
			fmt->shift = MLX90394_SHIFT_MAGN_HIGH_RANGE;
		}
		break;
	case SENSOR_CHAN_AMBIENT_TEMP:
		fmt->micro_per_lsb = (int32_t)MLX90394_MICRO_CELSIUS_PER_BIT;
		fmt->shift = MLX90394_SHIFT_TEMP;
		break;
	default:
		return -ENOTSUP;
	}

	fmt->measured = edata->header.channel;

	/* This sensor lacks a FIFO: the buffer holds a single sample */
	*frames = (struct sensor_raw_frames){
		.frames = edata->readings,
		.size = sizeof(edata->readings),
		.frame_size = sizeof(edata->readings),
		.decode_frame = mlx90394_decode_frame,
		.user_data = fmt,
		.timestamp_ns = edata->header.timestamp,
		.shift = fmt->shift,
	};

	return 0;
}

static int mlx90394_decoder_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec channel,
					    uint16_t *frame_count)
{
	struct sensor_raw_frames frames;
	struct mlx90394_frame_format fmt;
	int rc;

	rc = mlx90394_get_frames(buffer, channel, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_raw_frames_count(&frames, channel, frame_count);
}

static int mlx90394_decoder_get_size_info(struct sensor_chan_spec channel, size_t *base_size,
					  size_t *frame_size)
{
	if (channel.chan_idx != 0U || mlx90394_channel_readings(channel.chan_type) == 0U) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(channel, 0U, base_size, frame_size);
}

static int mlx90394_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec channel,
				   uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_raw_frames frames;
	struct mlx90394_frame_format fmt;
	int rc;

	rc = mlx90394_get_frames(buffer, channel, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames(&frames, channel, fit, max_count, data_out);
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = mlx90394_decoder_get_frame_count,
	.get_size_info = mlx90394_decoder_get_size_info,
	.decode = mlx90394_decoder_decode,
};

int mlx90394_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
