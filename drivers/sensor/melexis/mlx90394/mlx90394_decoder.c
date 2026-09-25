/*
 * Copyright (c) 2024 Florian Weber <Florian.Weber@live.de>
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mlx90394.h"
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#define DT_DRV_COMPAT melexis_mlx90394

/* Index of each value in mlx90394_encoded_data.readings, in 16-bit words */
#define MLX90394_READING_X    0U
#define MLX90394_READING_Y    1U
#define MLX90394_READING_Z    2U
#define MLX90394_READING_TEMP 3U

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

static int16_t mlx90394_reading_get(const struct mlx90394_encoded_data *edata, uint8_t idx)
{
	return (int16_t)sys_get_le16(&edata->readings[idx * sizeof(uint16_t)]);
}

static int mlx90394_decoder_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec channel,
					    uint16_t *frame_count)
{
	const struct mlx90394_encoded_data *edata = (const struct mlx90394_encoded_data *)buffer;

	/* This sensor lacks a FIFO: the buffer holds one frame if the channel was measured */
	*frame_count =
		mlx90394_channel_measured(edata->header.channel, channel.chan_type) ? 1U : 0U;
	return 0;
}

static int mlx90394_decoder_get_size_info(struct sensor_chan_spec channel, size_t *base_size,
					  size_t *frame_size)
{
	switch (channel.chan_type) {
	case SENSOR_CHAN_MAGN_XYZ: {
		*base_size = sizeof(struct sensor_three_axis_data);
		*frame_size = sizeof(struct sensor_three_axis_sample_data);
	} break;
	case SENSOR_CHAN_MAGN_X:
	case SENSOR_CHAN_MAGN_Y:
	case SENSOR_CHAN_MAGN_Z:
	case SENSOR_CHAN_AMBIENT_TEMP: {
		*base_size = sizeof(struct sensor_q31_data);
		*frame_size = sizeof(struct sensor_q31_sample_data);
	} break;
	default:
		return -ENOTSUP;
	}
	return 0;
}

static int mlx90394_convert_raw_magn_to_q31(int16_t reading, q31_t *out,
					    const enum mlx90394_reg_config_val config_val)
{
	int64_t intermediate;

	if (config_val == MLX90394_CTRL2_CONFIG_HIGH_SENSITIVITY_LOW_NOISE) {
		intermediate = ((int64_t)reading * MLX90394_HIGH_SENSITIVITY_MICRO_GAUSS_PER_BIT) *
			       ((int64_t)INT32_MAX + 1) /
			       ((1 << MLX90394_SHIFT_MAGN_HIGH_SENSITIVITY) * INT64_C(1000000));
	} else {
		intermediate = ((int64_t)reading * MLX90394_HIGH_RANGE_MICRO_GAUSS_PER_BIT) *
			       ((int64_t)INT32_MAX + 1) /
			       ((1 << MLX90394_SHIFT_MAGN_HIGH_RANGE) * INT64_C(1000000));
	}

	*out = CLAMP(intermediate, INT32_MIN, INT32_MAX);
	return 0;
}
static int mlx90394_convert_raw_temp_to_q31(int16_t reading, q31_t *out)
{

	int64_t intermediate = ((int64_t)reading * MLX90394_MICRO_CELSIUS_PER_BIT) *
			       ((int64_t)INT32_MAX + 1) /
			       ((1 << MLX90394_SHIFT_TEMP) * INT64_C(1000000));

	*out = CLAMP(intermediate, INT32_MIN, INT32_MAX);
	return 0;
}

static int mlx90394_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec channel,
				   uint32_t *fit, uint16_t max_count, void *data_out)
{
	const struct mlx90394_encoded_data *edata = (const struct mlx90394_encoded_data *)buffer;

	if (*fit != 0) {
		return 0;
	}

	if (!mlx90394_channel_measured(edata->header.channel, channel.chan_type)) {
		return -ENODATA;
	}

	switch (channel.chan_type) {
	case SENSOR_CHAN_MAGN_X:
	case SENSOR_CHAN_MAGN_Y:
	case SENSOR_CHAN_MAGN_Z: {
		struct sensor_q31_data *out = data_out;

		out->header.base_timestamp_ns = edata->header.timestamp;
		out->header.reading_count = 1;
		if (edata->header.config_val == MLX90394_CTRL2_CONFIG_HIGH_SENSITIVITY_LOW_NOISE) {
			out->shift = MLX90394_SHIFT_MAGN_HIGH_SENSITIVITY;
		} else {
			out->shift = MLX90394_SHIFT_MAGN_HIGH_RANGE;
		}

		mlx90394_convert_raw_magn_to_q31(
			mlx90394_reading_get(edata,
					     (uint8_t)(channel.chan_type - SENSOR_CHAN_MAGN_X)),
			&out->readings[0].value, edata->header.config_val);
		*fit = 1;
	} break;
	case SENSOR_CHAN_MAGN_XYZ: {
		struct sensor_three_axis_data *out = data_out;

		out->header.base_timestamp_ns = edata->header.timestamp;
		out->header.reading_count = 1;
		if (edata->header.config_val == MLX90394_CTRL2_CONFIG_HIGH_SENSITIVITY_LOW_NOISE) {
			out->shift = MLX90394_SHIFT_MAGN_HIGH_SENSITIVITY;
		} else {
			out->shift = MLX90394_SHIFT_MAGN_HIGH_RANGE;
		}

		mlx90394_convert_raw_magn_to_q31(mlx90394_reading_get(edata, MLX90394_READING_X),
						 &out->readings[0].x, edata->header.config_val);
		mlx90394_convert_raw_magn_to_q31(mlx90394_reading_get(edata, MLX90394_READING_Y),
						 &out->readings[0].y, edata->header.config_val);
		mlx90394_convert_raw_magn_to_q31(mlx90394_reading_get(edata, MLX90394_READING_Z),
						 &out->readings[0].z, edata->header.config_val);
		*fit = 1;
	} break;
	case SENSOR_CHAN_AMBIENT_TEMP: {
		struct sensor_q31_data *out = data_out;

		out->header.base_timestamp_ns = edata->header.timestamp;
		out->header.reading_count = 1;
		out->shift = MLX90394_SHIFT_TEMP;
		mlx90394_convert_raw_temp_to_q31(mlx90394_reading_get(edata, MLX90394_READING_TEMP),
						 &out->readings[0].temperature);
		*fit = 1;
	} break;
	default:
		return -ENOTSUP;
	}
	return 1;
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
