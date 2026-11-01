/*
 * Copyright (c) 2023 Google LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "icm4268x_decoder.h"
#include "icm4268x_reg.h"
#include "icm4268x.h"
#include <errno.h>

#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor_clock.h>

LOG_MODULE_REGISTER(ICM4268X_DECODER, CONFIG_SENSOR_LOG_LEVEL);

#define DT_DRV_COMPAT invensense_icm4268x

static const struct icm4268x_reg_val_pair table_accel_shift_to_reg[][5] = {
	[ICM4268X_VARIANT_ICM42688] = {
		{.val = 8, .reg = ICM42688_DT_ACCEL_FS_16},
		{.val = 7, .reg = ICM42688_DT_ACCEL_FS_8},
		{.val = 6, .reg = ICM42688_DT_ACCEL_FS_4},
		{.val = 5, .reg = ICM42688_DT_ACCEL_FS_2},
	},
	[ICM4268X_VARIANT_ICM42686] = {
		{.val = 9, .reg = ICM42686_DT_ACCEL_FS_32},
		{.val = 8, .reg = ICM42686_DT_ACCEL_FS_16},
		{.val = 7, .reg = ICM42686_DT_ACCEL_FS_8},
		{.val = 6, .reg = ICM42686_DT_ACCEL_FS_4},
		{.val = 5, .reg = ICM42686_DT_ACCEL_FS_2},
	},
	[ICM4268X_VARIANT_IIM42653] = {
		{.val = 9, .reg = IIM42653_DT_ACCEL_FS_32},
		{.val = 8, .reg = IIM42653_DT_ACCEL_FS_16},
		{.val = 7, .reg = IIM42653_DT_ACCEL_FS_8},
		{.val = 6, .reg = IIM42653_DT_ACCEL_FS_4},
	},
};

static const struct icm4268x_reg_val_pair table_gyro_shift_to_reg[][8] = {
	[ICM4268X_VARIANT_ICM42688] = {
		{.val = 6, .reg = ICM42688_DT_GYRO_FS_2000},
		{.val = 5, .reg = ICM42688_DT_GYRO_FS_1000},
		{.val = 4,  .reg = ICM42688_DT_GYRO_FS_500},
		{.val = 3,  .reg = ICM42688_DT_GYRO_FS_250},
		{.val = 2,  .reg = ICM42688_DT_GYRO_FS_125},
		{.val = 1,   .reg = ICM42688_DT_GYRO_FS_62_5},
		{.val = 0,   .reg = ICM42688_DT_GYRO_FS_31_25},
		{.val = -1,   .reg = ICM42688_DT_GYRO_FS_15_625},
	},
	[ICM4268X_VARIANT_ICM42686] = {
		{.val = 7, .reg = ICM42686_DT_GYRO_FS_4000},
		{.val = 6, .reg = ICM42686_DT_GYRO_FS_2000},
		{.val = 5, .reg = ICM42686_DT_GYRO_FS_1000},
		{.val = 4,  .reg = ICM42686_DT_GYRO_FS_500},
		{.val = 3,  .reg = ICM42686_DT_GYRO_FS_250},
		{.val = 2,  .reg = ICM42686_DT_GYRO_FS_125},
		{.val = 1,   .reg = ICM42686_DT_GYRO_FS_62_5},
		{.val = 0,   .reg = ICM42686_DT_GYRO_FS_31_25},
	},
	[ICM4268X_VARIANT_IIM42653] = {
		{.val = 7, .reg = IIM42653_DT_GYRO_FS_4000},
		{.val = 6, .reg = IIM42653_DT_GYRO_FS_2000},
		{.val = 5, .reg = IIM42653_DT_GYRO_FS_1000},
		{.val = 4,  .reg = IIM42653_DT_GYRO_FS_500},
		{.val = 3,  .reg = IIM42653_DT_GYRO_FS_250},
		{.val = 2,  .reg = IIM42653_DT_GYRO_FS_125},
		{.val = 1,   .reg = IIM42653_DT_GYRO_FS_62_5},
		{.val = 0,   .reg = IIM42653_DT_GYRO_FS_31_25},
	},
};

static int icm4268x_get_shift(enum sensor_channel channel, int accel_fs, int gyro_fs,
			      enum icm4268x_variant variant, int8_t *shift)
{
	switch (channel) {
	case SENSOR_CHAN_ACCEL_XYZ:
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z:
		for (uint8_t i  = 0 ; i < table_accel_fs_to_reg_array_size[variant] ; i++) {
			if (accel_fs == table_accel_shift_to_reg[variant][i].reg) {
				*shift = table_accel_shift_to_reg[variant][i].val;
				return 0;
			}
		}
		return -EINVAL;
	case SENSOR_CHAN_GYRO_XYZ:
	case SENSOR_CHAN_GYRO_X:
	case SENSOR_CHAN_GYRO_Y:
	case SENSOR_CHAN_GYRO_Z:
		for (uint8_t i  = 0 ; i < table_gyro_fs_to_reg_array_size[variant] ; i++) {
			if (gyro_fs == table_gyro_shift_to_reg[variant][i].reg) {
				*shift = table_gyro_shift_to_reg[variant][i].val;
				return 0;
			}
		}
		return -EINVAL;
	case SENSOR_CHAN_DIE_TEMP:
		*shift = 9;
		return 0;
	default:
		return -EINVAL;
	}
}

int icm4268x_convert_raw_to_q31(struct icm4268x_cfg *cfg, enum sensor_channel chan, int32_t reading,
				q31_t *out)
{
	int32_t whole;
	int32_t fraction;
	int64_t intermediate;
	int8_t shift;
	int rc;

	rc = icm4268x_get_shift(chan, cfg->accel_fs, cfg->gyro_fs, cfg->variant, &shift);
	if (rc != 0) {
		return rc;
	}

	switch (chan) {
	case SENSOR_CHAN_ACCEL_XYZ:
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z:
		icm4268x_accel_ms(cfg, reading, &whole, &fraction);
		break;
	case SENSOR_CHAN_GYRO_XYZ:
	case SENSOR_CHAN_GYRO_X:
	case SENSOR_CHAN_GYRO_Y:
	case SENSOR_CHAN_GYRO_Z:
		icm4268x_gyro_rads(cfg, reading, &whole, &fraction);
		break;
	case SENSOR_CHAN_DIE_TEMP:
		icm4268x_temp_c(reading, &whole, &fraction);
		break;
	default:
		return -ENOTSUP;
	}
	intermediate = ((int64_t)whole * INT64_C(1000000) + fraction);
	if (shift < 0) {
		intermediate =
			intermediate * ((int64_t)INT32_MAX + 1) * (1 << -shift) / INT64_C(1000000);
	} else {
		intermediate =
			intermediate * ((int64_t)INT32_MAX + 1) / ((1 << shift) * INT64_C(1000000));
	}
	*out = CLAMP(intermediate, INT32_MIN, INT32_MAX);

	return 0;
}

static int icm4268x_get_channel_position(enum sensor_channel chan)
{
	switch (chan) {
	case SENSOR_CHAN_DIE_TEMP:
		return 0;
	case SENSOR_CHAN_ACCEL_XYZ:
	case SENSOR_CHAN_ACCEL_X:
		return 1;
	case SENSOR_CHAN_ACCEL_Y:
		return 2;
	case SENSOR_CHAN_ACCEL_Z:
		return 3;
	case SENSOR_CHAN_GYRO_XYZ:
	case SENSOR_CHAN_GYRO_X:
		return 4;
	case SENSOR_CHAN_GYRO_Y:
		return 5;
	case SENSOR_CHAN_GYRO_Z:
		return 6;
	default:
		return 0;
	}
}

static uint8_t icm4268x_encode_channel(enum sensor_channel chan)
{
	uint8_t encode_bmask = 0;

	switch (chan) {
	case SENSOR_CHAN_DIE_TEMP:
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z:
	case SENSOR_CHAN_GYRO_X:
	case SENSOR_CHAN_GYRO_Y:
	case SENSOR_CHAN_GYRO_Z:
		encode_bmask = BIT(icm4268x_get_channel_position(chan));
		break;
	case SENSOR_CHAN_ACCEL_XYZ:
		encode_bmask = BIT(icm4268x_get_channel_position(SENSOR_CHAN_ACCEL_X)) |
			       BIT(icm4268x_get_channel_position(SENSOR_CHAN_ACCEL_Y)) |
			       BIT(icm4268x_get_channel_position(SENSOR_CHAN_ACCEL_Z));
		break;
	case SENSOR_CHAN_GYRO_XYZ:
		encode_bmask = BIT(icm4268x_get_channel_position(SENSOR_CHAN_GYRO_X)) |
			       BIT(icm4268x_get_channel_position(SENSOR_CHAN_GYRO_Y)) |
			       BIT(icm4268x_get_channel_position(SENSOR_CHAN_GYRO_Z));
		break;
	default:
		break;
	}

	return encode_bmask;
}

int icm4268x_encode(const struct device *dev, const struct sensor_chan_spec *const channels,
		    const size_t num_channels, uint8_t *buf)
{
	struct icm4268x_dev_data *data = dev->data;
	struct icm4268x_encoded_data *edata = (struct icm4268x_encoded_data *)buf;
	uint64_t cycles;
	int rc;

	edata->channels = 0;

	for (int i = 0; i < num_channels; i++) {
		edata->channels |= icm4268x_encode_channel(channels[i].chan_type);
	}

	rc = sensor_clock_get_cycles(&cycles);
	if (rc != 0) {
		return rc;
	}

	edata->header.is_fifo = false;
	edata->header.variant = data->cfg.variant;
	edata->header.accel_fs = data->cfg.accel_fs;
	edata->header.gyro_fs = data->cfg.gyro_fs;
	edata->header.axis_align[0] = data->cfg.axis_align[0];
	edata->header.axis_align[1] = data->cfg.axis_align[1];
	edata->header.axis_align[2] = data->cfg.axis_align[2];
	edata->header.timestamp = sensor_clock_cycles_to_ns(cycles);

	return 0;
}

#define IS_ACCEL(chan) ((chan) >= SENSOR_CHAN_ACCEL_X && (chan) <= SENSOR_CHAN_ACCEL_XYZ)
#define IS_GYRO(chan)  ((chan) >= SENSOR_CHAN_GYRO_X && (chan) <= SENSOR_CHAN_GYRO_XYZ)

static inline q31_t icm4268x_read_temperature_from_packet(const uint8_t *pkt)
{
	int32_t temperature;
	int32_t whole;
	int32_t fraction;

	/* Temperature always assumes a shift of 9 for a range of (-273,273) C */
	if (FIELD_GET(FIFO_HEADER_20, pkt[0]) == 1) {
		temperature = (int16_t)((pkt[0xd] << 8) | pkt[0xe]);

		icm4268x_temp_c(temperature, &whole, &fraction);
	} else {
		if (FIELD_GET(FIFO_HEADER_ACCEL, pkt[0]) == 1 &&
		    FIELD_GET(FIFO_HEADER_GYRO, pkt[0]) == 1) {
			temperature = (int8_t)pkt[0xd];
		} else {
			temperature = (int8_t)pkt[0x7];
		}

		int32_t sensitivity = 207;
		int32_t temperature100 = (temperature * 100) + (25 * sensitivity);

		whole = temperature100 / sensitivity;
		fraction = ((temperature100 - whole * sensitivity) * 1000000) / sensitivity;
	}
	__ASSERT_NO_MSG(whole >= -512 && whole <= 511);

	/*
	 * fraction * 2^22 / 10^6 is fraction * 2^16 / 15625, split so that the products fit in
	 * 32 bits. Both parts have the sign of fraction, so the result is truncated the same way.
	 */
	int32_t fraction_q22 = (fraction / 15625) * 65536 + (fraction % 15625) * 65536 / 15625;

	return (q31_t)(((int64_t)whole << 22) + fraction_q22);
}

static int icm4268x_read_imu_from_packet(const uint8_t *pkt, bool is_accel, q31_t out[3])
{
	static const uint32_t scale[2][2] = {
		/* low-res,	hi-res */
		{35744,		2235}, /* gyro */
		{40168,		2511}, /* accel */
	};
	const bool is_hires = FIELD_GET(FIFO_HEADER_20, pkt[0]) == 1;
	const uint32_t lsb_scale = scale[is_accel][is_hires];
	const uint8_t *data = &pkt[1];
	int rc = 0;

	if (!is_accel && FIELD_GET(FIFO_HEADER_ACCEL, pkt[0]) == 1) {
		data += 6;
	}

	for (int i = 0; i < 3; i++) {
		uint32_t unsigned_value = (data[2 * i] << 8) | data[2 * i + 1];
		int32_t signed_value;

		if (is_hires) {
			uint8_t lsb = pkt[17 + i];

			/* Accel in the high nibble, gyro in the low one */
			lsb = is_accel ? FIELD_GET(GENMASK(7, 4), lsb)
				       : FIELD_GET(GENMASK(3, 0), lsb);
			unsigned_value = (unsigned_value << 4) | lsb;
			signed_value = unsigned_value | (0 - (unsigned_value & BIT(19)));

			/*
			 * By default, INTF_CONFIG0 is set to 0x30 and thus FIFO_HOLD_LAST_DATA_EN
			 * is set to 0. For 20-bit FIFO packets, -524288 indicates invalid data.
			 *
			 * At the time of writing, INTF_CONFIG0 is not configured explicitly.
			 *
			 * TODO: Enable/disable this check based on FIFO_HOLD_LAST_DATA_EN if
			 * INTF_CONFIG0 is configured explicitly.
			 */
			if (signed_value == -524288) {
				rc = -ENODATA;
				continue;
			}
		} else {
			signed_value = unsigned_value | (0 - (unsigned_value & BIT(15)));
		}

		out[i] = (q31_t)(signed_value * lsb_scale);
	}

	return rc;
}

static const uint32_t accel_period_ns[] = {
	[ICM42688_DT_ACCEL_ODR_1_5625] = UINT32_C(10000000000000) / 15625,
	[ICM42688_DT_ACCEL_ODR_3_125] = UINT32_C(10000000000000) / 31250,
	[ICM42688_DT_ACCEL_ODR_6_25] = UINT32_C(10000000000000) / 62500,
	[ICM42688_DT_ACCEL_ODR_12_5] = UINT32_C(10000000000000) / 125000,
	[ICM42688_DT_ACCEL_ODR_25] = UINT32_C(1000000000) / 25,
	[ICM42688_DT_ACCEL_ODR_50] = UINT32_C(1000000000) / 50,
	[ICM42688_DT_ACCEL_ODR_100] = UINT32_C(1000000000) / 100,
	[ICM42688_DT_ACCEL_ODR_200] = UINT32_C(1000000000) / 200,
	[ICM42688_DT_ACCEL_ODR_500] = UINT32_C(1000000000) / 500,
	[ICM42688_DT_ACCEL_ODR_1000] = UINT32_C(1000000),
	[ICM42688_DT_ACCEL_ODR_2000] = UINT32_C(1000000) / 2,
	[ICM42688_DT_ACCEL_ODR_4000] = UINT32_C(1000000) / 4,
	[ICM42688_DT_ACCEL_ODR_8000] = UINT32_C(1000000) / 8,
	[ICM42688_DT_ACCEL_ODR_16000] = UINT32_C(1000000) / 16,
	[ICM42688_DT_ACCEL_ODR_32000] = UINT32_C(1000000) / 32,
};

static const uint32_t gyro_period_ns[] = {
	[ICM42688_DT_GYRO_ODR_12_5] = UINT32_C(10000000000000) / 125000,
	[ICM42688_DT_GYRO_ODR_25] = UINT32_C(1000000000) / 25,
	[ICM42688_DT_GYRO_ODR_50] = UINT32_C(1000000000) / 50,
	[ICM42688_DT_GYRO_ODR_100] = UINT32_C(1000000000) / 100,
	[ICM42688_DT_GYRO_ODR_200] = UINT32_C(1000000000) / 200,
	[ICM42688_DT_GYRO_ODR_500] = UINT32_C(1000000000) / 500,
	[ICM42688_DT_GYRO_ODR_1000] = UINT32_C(1000000),
	[ICM42688_DT_GYRO_ODR_2000] = UINT32_C(1000000) / 2,
	[ICM42688_DT_GYRO_ODR_4000] = UINT32_C(1000000) / 4,
	[ICM42688_DT_GYRO_ODR_8000] = UINT32_C(1000000) / 8,
	[ICM42688_DT_GYRO_ODR_16000] = UINT32_C(1000000) / 16,
	[ICM42688_DT_GYRO_ODR_32000] = UINT32_C(1000000) / 32,
};

/* Spacing of the FIFO frames of one sensor */
struct icm4268x_fifo_period {
	uint64_t whole_ns;
	uint32_t remainder;
	uint32_t period_ns;
	uint16_t rtc_freq;
};

/*
 * When ODR is set to r and an external clock with frequency f is used,
 * the actual ODR = f * r / 32000, so frame n comes period * n * 32000 / f ns
 * after the first one. Split period * 32000 into whole_ns * f + remainder once
 * per batch so that each frame only needs a 32-bit division.
 */
static void icm4268x_fifo_period_init(struct icm4268x_fifo_period *fp, uint32_t period_ns,
				      uint16_t rtc_freq)
{
	uint32_t part = (period_ns % rtc_freq) * 32000U;

	fp->whole_ns = (uint64_t)(period_ns / rtc_freq) * 32000U + part / rtc_freq;
	fp->remainder = part % rtc_freq;
	fp->period_ns = period_ns;
	fp->rtc_freq = rtc_freq;
}

static uint64_t icm4268x_fifo_frame_delta(const struct icm4268x_fifo_period *fp, int frame)
{
	if (frame < 0) {
		/* Temperature frame ahead of any gyro frame, wraps around as an unsigned value */
		return (uint64_t)fp->period_ns * frame * 32000 / fp->rtc_freq;
	}

	/* fifo_count is 11 bits wide, so frame < 256 and remainder * frame fits in 32 bits */
	return fp->whole_ns * (uint32_t)frame + fp->remainder * (uint32_t)frame / fp->rtc_freq;
}

static uint8_t icm4268x_fifo_frame_size(uint8_t header)
{
	if (FIELD_GET(FIFO_HEADER_20, header) == 1) {
		return 20;
	}
	if (FIELD_GET(FIFO_HEADER_ACCEL, header) == 1 && FIELD_GET(FIFO_HEADER_GYRO, header) == 1) {
		return 16;
	}
	return 8;
}

static int icm4268x_fifo_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				uint32_t *fit, uint16_t max_count, void *data_out)
{
	const struct icm4268x_fifo_data *edata = (const struct icm4268x_fifo_data *)buffer;
	const uint8_t *buffer_end = buffer + sizeof(struct icm4268x_fifo_data) + edata->fifo_count;
	struct icm4268x_fifo_period period;
	int accel_frame_count = 0;
	int gyro_frame_count = 0;
	uint16_t total_frames = 0;
	uint8_t chan_header;
	int count = 0;
	int8_t shift = 0;
	int shift_rc;
	int rc = 0;

	if ((uintptr_t)buffer_end <= *fit || chan_spec.chan_idx != 0) {
		return 0;
	}

	/* Header bits of the frames that carry a sample of the requested channel */
	if (IS_ACCEL(chan_spec.chan_type)) {
		chan_header = FIFO_HEADER_ACCEL;
	} else if (IS_GYRO(chan_spec.chan_type)) {
		chan_header = FIFO_HEADER_GYRO;
	} else if (chan_spec.chan_type == SENSOR_CHAN_DIE_TEMP) {
		chan_header = FIFO_HEADER_ACCEL | FIFO_HEADER_GYRO;
	} else {
		chan_header = 0;
	}

	if (IS_ACCEL(chan_spec.chan_type) || chan_spec.chan_type == SENSOR_CHAN_DIE_TEMP) {
		icm4268x_fifo_period_init(&period, accel_period_ns[edata->accel_odr],
					  edata->rtc_freq);
	} else {
		icm4268x_fifo_period_init(&period, gyro_period_ns[edata->gyro_odr],
					  edata->rtc_freq);
	}

	shift_rc = icm4268x_get_shift(chan_spec.chan_type, edata->header.accel_fs,
				      edata->header.gyro_fs, edata->header.variant, &shift);

	buffer += sizeof(struct icm4268x_fifo_data);
	while (count < max_count && buffer < buffer_end) {
		const bool has_accel = FIELD_GET(FIFO_HEADER_ACCEL, buffer[0]) == 1;
		const bool has_gyro = FIELD_GET(FIFO_HEADER_GYRO, buffer[0]) == 1;
		const uint8_t *frame_end = buffer + icm4268x_fifo_frame_size(buffer[0]);

		if ((buffer[0] & chan_header) != 0) {
			total_frames++;
		}
		if (has_accel) {
			accel_frame_count++;
		}
		if (has_gyro) {
			gyro_frame_count++;
		}

		if ((uintptr_t)buffer < *fit) {
			/* This frame was already decoded, move on to the next frame */
			buffer = frame_end;
			continue;
		}
		if (chan_spec.chan_type == SENSOR_CHAN_DIE_TEMP) {
			struct sensor_q31_data *data = (struct sensor_q31_data *)data_out;
			uint64_t ts_delta;

			if (has_accel) {
				ts_delta = icm4268x_fifo_frame_delta(&period,
								     accel_frame_count - 1);
			} else {
				struct icm4268x_fifo_period gyro_period;

				icm4268x_fifo_period_init(&gyro_period,
							  gyro_period_ns[edata->gyro_odr],
							  edata->rtc_freq);
				ts_delta = icm4268x_fifo_frame_delta(&gyro_period,
								     gyro_frame_count - 1);
			}

			/*
			 * TODO: For some extreme combination of ODR and FIFO count, using uint32_t
			 * to store timestamp delta will overflow. Better error reporting?
			 */
			if (ts_delta > UINT32_MAX) {
				LOG_ERR("Timestamp delta overflow");
				buffer = frame_end;
				continue;
			}

			data->readings[count].timestamp_delta = ts_delta;

			data->shift = 9;
			data->readings[count].temperature =
				icm4268x_read_temperature_from_packet(buffer);
		/* coverity[unsigned_compare] */
		} else if (IS_ACCEL(chan_spec.chan_type) && has_accel) {
			/* Decode accel */
			struct sensor_three_axis_data *data =
				(struct sensor_three_axis_data *)data_out;
			uint64_t ts_delta;

			if (shift_rc == 0) {
				data->shift = shift;
			}

			ts_delta = icm4268x_fifo_frame_delta(&period, accel_frame_count - 1);

			/*
			 * TODO: For some extreme combination of ODR and FIFO count, using uint32_t
			 * to store timestamp delta will overflow. Better error reporting?
			 */
			if (ts_delta > UINT32_MAX) {
				LOG_ERR("Timestamp delta overflow");
				buffer = frame_end;
				continue;
			}

			data->readings[count].timestamp_delta = ts_delta;

			q31_t reading[3];

			rc = icm4268x_read_imu_from_packet(buffer, true, reading);

			for (int i = 0; i < 3; i++) {
				data->readings[count].values[i] =
					edata->header.axis_align[i].sign*
					reading[edata->header.axis_align[i].index];
			}

			if (rc != 0) {
				accel_frame_count--;
				buffer = frame_end;
				continue;
			}
		} else if (IS_GYRO(chan_spec.chan_type) && has_gyro) {
			/* Decode gyro */
			struct sensor_three_axis_data *data =
				(struct sensor_three_axis_data *)data_out;
			uint64_t ts_delta;

			if (shift_rc == 0) {
				data->shift = shift;
			}

			ts_delta = icm4268x_fifo_frame_delta(&period, gyro_frame_count - 1);

			/*
			 * TODO: For some extreme combination of ODR and FIFO count, using uint32_t
			 * to store timestamp delta will overflow. Better error reporting?
			 */
			if (ts_delta > UINT32_MAX) {
				LOG_ERR("Timestamp delta overflow");
				buffer = frame_end;
				continue;
			}

			data->readings[count].timestamp_delta = ts_delta;

			q31_t reading[3];

			rc = icm4268x_read_imu_from_packet(buffer, false, reading);

			for (int i = 0; i < 3; i++) {
				data->readings[count].values[i] =
					edata->header.axis_align[i].sign*
					reading[edata->header.axis_align[i].index];
			}

			if (rc != 0) {
				gyro_frame_count--;
				buffer = frame_end;
				continue;
			}
		} else {
			CODE_UNREACHABLE;
		}
		buffer = frame_end;
		*fit = (uintptr_t)frame_end;
		count++;
	}

	/* The frames left after max_count still move the first one back in time */
	while (buffer < buffer_end) {
		if ((buffer[0] & chan_header) != 0) {
			total_frames++;
		}
		buffer += icm4268x_fifo_frame_size(buffer[0]);
	}

	((struct sensor_data_header *)data_out)->base_timestamp_ns =
		edata->header.timestamp -
		(total_frames > 0 ? (total_frames - 1) : 0) * period.whole_ns;

	return count;
}

static int icm4268x_one_shot_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				    uint32_t *fit, uint16_t max_count, void *data_out)
{
	const struct icm4268x_encoded_data *edata = (const struct icm4268x_encoded_data *)buffer;
	const struct icm4268x_decoder_header *header = &edata->header;
	struct icm4268x_cfg cfg = {
		.accel_fs = edata->header.accel_fs,
		.gyro_fs = edata->header.gyro_fs,
		.variant = edata->header.variant,
	};
	uint8_t channel_request;
	int rc;

	if (*fit != 0) {
		return 0;
	}
	if (max_count == 0 || chan_spec.chan_idx != 0) {
		return -EINVAL;
	}

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z:
	case SENSOR_CHAN_GYRO_X:
	case SENSOR_CHAN_GYRO_Y:
	case SENSOR_CHAN_GYRO_Z:
	case SENSOR_CHAN_DIE_TEMP: {
		channel_request = icm4268x_encode_channel(chan_spec.chan_type);
		if ((channel_request & edata->channels) != channel_request) {
			return -ENODATA;
		}

		struct sensor_q31_data *out = data_out;

		out->header.base_timestamp_ns = edata->header.timestamp;
		out->header.reading_count = 1;

		rc = icm4268x_get_shift(chan_spec.chan_type, header->accel_fs, header->gyro_fs,
					header->variant, &out->shift);
		if (rc != 0) {
			return -EINVAL;
		}

		if (chan_spec.chan_type == SENSOR_CHAN_DIE_TEMP) {
			icm4268x_convert_raw_to_q31(
				&cfg, chan_spec.chan_type,
				edata->readings[icm4268x_get_channel_position(chan_spec.chan_type)],
				&out->readings[0].value);
		} else {
			int8_t pos = icm4268x_get_channel_position(chan_spec.chan_type);
			int8_t base = pos >= 4 ? 4 : 1;
			int8_t axis = pos - base;

			icm4268x_convert_raw_to_q31(
				&cfg, chan_spec.chan_type,
				header->axis_align[axis].sign *
					edata->readings[base + header->axis_align[axis].index],
				&out->readings[0].value);
		}
		*fit = 1;
		return 1;
	}
	case SENSOR_CHAN_ACCEL_XYZ:
	case SENSOR_CHAN_GYRO_XYZ: {
		channel_request = icm4268x_encode_channel(chan_spec.chan_type);
		if ((channel_request & edata->channels) != channel_request) {
			return -ENODATA;
		}

		struct sensor_three_axis_data *out = data_out;

		out->header.base_timestamp_ns = edata->header.timestamp;
		out->header.reading_count = 1;
		rc = icm4268x_get_shift(chan_spec.chan_type, header->accel_fs, header->gyro_fs,
					header->variant, &out->shift);
		if (rc != 0) {
			return -EINVAL;
		}

		int8_t base = icm4268x_get_channel_position(chan_spec.chan_type - 3);

		icm4268x_convert_raw_to_q31(
			&cfg, chan_spec.chan_type - 3,
			header->axis_align[0].sign *
				edata->readings[base + header->axis_align[0].index],
			&out->readings[0].x);
		icm4268x_convert_raw_to_q31(
			&cfg, chan_spec.chan_type - 2,
			header->axis_align[1].sign *
				edata->readings[base + header->axis_align[1].index],
			&out->readings[0].y);
		icm4268x_convert_raw_to_q31(
			&cfg, chan_spec.chan_type - 1,
			header->axis_align[2].sign *
				edata->readings[base + header->axis_align[2].index],
			&out->readings[0].z);
		*fit = 1;
		return 1;
	}
	default:
		return -EINVAL;
	}
}

static int icm4268x_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				   uint32_t *fit, uint16_t max_count, void *data_out)
{
	const struct icm4268x_decoder_header *header =
		(const struct icm4268x_decoder_header *)buffer;

	if (header->is_fifo) {
		return icm4268x_fifo_decode(buffer, chan_spec, fit, max_count, data_out);
	}
	return icm4268x_one_shot_decode(buffer, chan_spec, fit, max_count, data_out);
}

static int icm4268x_decoder_get_frame_count(const uint8_t *buffer,
					    struct sensor_chan_spec chan_spec,
					    uint16_t *frame_count)
{
	const struct icm4268x_fifo_data *data = (const struct icm4268x_fifo_data *)buffer;
	const struct icm4268x_encoded_data *enc_data = (const struct icm4268x_encoded_data *)buffer;
	const struct icm4268x_decoder_header *header = &data->header;

	if (chan_spec.chan_idx != 0) {
		return -ENOTSUP;
	}

	uint8_t channel_request = icm4268x_encode_channel(chan_spec.chan_type);


	if ((!enc_data->header.is_fifo) &&
	    (enc_data->channels & channel_request) != channel_request) {
		return -ENODATA;
	}

	if (!header->is_fifo) {
		switch (chan_spec.chan_type) {
		case SENSOR_CHAN_ACCEL_X:
		case SENSOR_CHAN_ACCEL_Y:
		case SENSOR_CHAN_ACCEL_Z:
		case SENSOR_CHAN_ACCEL_XYZ:
		case SENSOR_CHAN_GYRO_X:
		case SENSOR_CHAN_GYRO_Y:
		case SENSOR_CHAN_GYRO_Z:
		case SENSOR_CHAN_GYRO_XYZ:
		case SENSOR_CHAN_DIE_TEMP:
			*frame_count = 1;
			return 0;
		default:
			return -ENOTSUP;
		}
		return 0;
	}

	/* Skip the header */
	buffer += sizeof(struct icm4268x_fifo_data);

	uint16_t count = 0;
	const uint8_t *end = buffer + data->fifo_count;

	while (buffer < end) {
		bool is_20b = FIELD_GET(FIFO_HEADER_20, buffer[0]);
		int size = is_20b ? 3 : 2;

		if (FIELD_GET(FIFO_HEADER_ACCEL, buffer[0])) {
			size += 6;
		}
		if (FIELD_GET(FIFO_HEADER_GYRO, buffer[0])) {
			size += 6;
		}
		if (FIELD_GET(FIFO_HEADER_TIMESTAMP_FSYNC, buffer[0])) {
			size += 2;
		}
		if (is_20b) {
			size += 3;
		}

		buffer += size;
		++count;
	}

	*frame_count = count;
	return 0;
}

static int icm4268x_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					  size_t *frame_size)
{
	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_ACCEL_XYZ:
	case SENSOR_CHAN_GYRO_XYZ:
		*base_size = sizeof(struct sensor_three_axis_data);
		*frame_size = sizeof(struct sensor_three_axis_sample_data);
		return 0;
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z:
	case SENSOR_CHAN_GYRO_X:
	case SENSOR_CHAN_GYRO_Y:
	case SENSOR_CHAN_GYRO_Z:
	case SENSOR_CHAN_DIE_TEMP:
		*base_size = sizeof(struct sensor_q31_data);
		*frame_size = sizeof(struct sensor_q31_sample_data);
		return 0;
	default:
		return -ENOTSUP;
	}
}

static bool icm4268x_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	const struct icm4268x_fifo_data *edata = (const struct icm4268x_fifo_data *)buffer;

	if (!edata->header.is_fifo) {
		return false;
	}

	switch (trigger) {
	case SENSOR_TRIG_DATA_READY:
		return FIELD_GET(BIT_DATA_RDY_INT, edata->int_status);
	case SENSOR_TRIG_FIFO_WATERMARK:
		return FIELD_GET(BIT_FIFO_THS_INT, edata->int_status);
	case SENSOR_TRIG_FIFO_FULL:
		return FIELD_GET(BIT_FIFO_FULL_INT, edata->int_status);
	default:
		return false;
	}
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = icm4268x_decoder_get_frame_count,
	.get_size_info = icm4268x_decoder_get_size_info,
	.decode = icm4268x_decoder_decode,
	.has_trigger = icm4268x_decoder_has_trigger,
};

int icm4268x_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
