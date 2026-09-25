/*
 * Copyright (c) 2023 Google LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "icm4268x_decoder.h"
#include "icm4268x_reg.h"
#include "icm4268x.h"
#include <errno.h>

#include <zephyr/drivers/sensor_clock.h>
#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/sys/byteorder.h>

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

enum icm4268x_sensor {
	ICM4268X_SENSOR_ACCEL = 0,
	ICM4268X_SENSOR_GYRO = 1,
	ICM4268X_SENSOR_TEMP = 2,
};

/* Offsets in the one-shot data: TEMP_DATA1 to GYRO_DATA_Z0 registers, big-endian */
#define ICM4268X_ONE_SHOT_TEMP_OFFSET  0U
#define ICM4268X_ONE_SHOT_ACCEL_OFFSET 2U
#define ICM4268X_ONE_SHOT_GYRO_OFFSET  8U

/* FIFO packet sizes and offsets */
#define ICM4268X_FIFO_PACKET_20_SIZE   20
#define ICM4268X_FIFO_PACKET_16_SIZE   16
#define ICM4268X_FIFO_PACKET_8_SIZE    8
#define ICM4268X_FIFO_DATA_OFFSET      1U
#define ICM4268X_FIFO_TEMP_OFFSET_16   13U
#define ICM4268X_FIFO_TEMP_OFFSET_8    7U
#define ICM4268X_FIFO_EXTENSION_OFFSET 17U
#define ICM4268X_FIFO_EXTENSION_ACCEL  GENMASK(7, 4)
#define ICM4268X_FIFO_EXTENSION_GYRO   GENMASK(3, 0)
#define ICM4268X_FIFO_20_BITS_INVALID  (-524288)

/*
 * Temperature sensitivity in hundredths of LSB/C: 132.48 LSB/C in the data registers and in
 * 20-bit FIFO packets, 2.07 LSB/C in other FIFO packets, with 0 at 25 C. The shift covers
 * (-512, 512) C.
 */
#define ICM4268X_TEMP_SHIFT    9
#define ICM4268X_TEMP_SENS_16  13248
#define ICM4268X_TEMP_SENS_8   207
#define ICM4268X_TEMP_OFFSET_C 25

/* pi approximated by 355 / 113 (relative error 8.5e-8), so that the ratios fit in 32 bits */
#define ICM4268X_PI_NUM 355
#define ICM4268X_PI_DEN 113

/* A 20-bit FIFO sample is a 16-bit sample with 4 more fractional bits */
#define ICM4268X_FIFO_20_BITS_EXTRA_SHIFT 4

/*
 * One LSB of a 16-bit FIFO sample, as num / den of the unit at a reference shift. The shift
 * tracks the full scale, which is 2^shift / 16 g for the accelerometer and 2^shift * 31.25 dps
 * for the gyroscope, so the q31 value of a sample does not depend on the full scale. The
 * reference is +/-16 g (shift 8) and +/-2000 dps (shift 6).
 */
static const struct {
	int32_t num;
	int32_t den;
	int8_t shift;
} fifo_scale[] = {
	/* 16 g / 32768: SENSOR_G / (2048 * 1000000) m/s^2 */
	[ICM4268X_SENSOR_ACCEL] = {SENSOR_G / 10, 2048 * 100000, 8},
	/* 2000 dps / 32768: 2000 * pi / (180 * 32768) rad/s */
	[ICM4268X_SENSOR_GYRO] = {2000 * ICM4268X_PI_NUM, ICM4268X_PI_DEN * 180 * 32768, 6},
};

/* Periods indexed by the ODR devicetree values; 1.5625 Hz is not a whole number of mHz */
static const uint64_t accel_period_ns[] = {
	[ICM42688_DT_ACCEL_ODR_1_5625] = SENSOR_ODR_MHZ_TO_PERIOD_NS(3125) * 2U,
	[ICM42688_DT_ACCEL_ODR_3_125] = SENSOR_ODR_MHZ_TO_PERIOD_NS(3125),
	[ICM42688_DT_ACCEL_ODR_6_25] = SENSOR_ODR_MHZ_TO_PERIOD_NS(6250),
	[ICM42688_DT_ACCEL_ODR_12_5] = SENSOR_ODR_MHZ_TO_PERIOD_NS(12500),
	[ICM42688_DT_ACCEL_ODR_25] = SENSOR_ODR_MHZ_TO_PERIOD_NS(25000),
	[ICM42688_DT_ACCEL_ODR_50] = SENSOR_ODR_MHZ_TO_PERIOD_NS(50000),
	[ICM42688_DT_ACCEL_ODR_100] = SENSOR_ODR_MHZ_TO_PERIOD_NS(100000),
	[ICM42688_DT_ACCEL_ODR_200] = SENSOR_ODR_MHZ_TO_PERIOD_NS(200000),
	[ICM42688_DT_ACCEL_ODR_500] = SENSOR_ODR_MHZ_TO_PERIOD_NS(500000),
	[ICM42688_DT_ACCEL_ODR_1000] = SENSOR_ODR_MHZ_TO_PERIOD_NS(1000000),
	[ICM42688_DT_ACCEL_ODR_2000] = SENSOR_ODR_MHZ_TO_PERIOD_NS(2000000),
	[ICM42688_DT_ACCEL_ODR_4000] = SENSOR_ODR_MHZ_TO_PERIOD_NS(4000000),
	[ICM42688_DT_ACCEL_ODR_8000] = SENSOR_ODR_MHZ_TO_PERIOD_NS(8000000),
	[ICM42688_DT_ACCEL_ODR_16000] = SENSOR_ODR_MHZ_TO_PERIOD_NS(16000000),
	[ICM42688_DT_ACCEL_ODR_32000] = SENSOR_ODR_MHZ_TO_PERIOD_NS(32000000),
};

static const uint64_t gyro_period_ns[] = {
	[ICM42688_DT_GYRO_ODR_12_5] = SENSOR_ODR_MHZ_TO_PERIOD_NS(12500),
	[ICM42688_DT_GYRO_ODR_25] = SENSOR_ODR_MHZ_TO_PERIOD_NS(25000),
	[ICM42688_DT_GYRO_ODR_50] = SENSOR_ODR_MHZ_TO_PERIOD_NS(50000),
	[ICM42688_DT_GYRO_ODR_100] = SENSOR_ODR_MHZ_TO_PERIOD_NS(100000),
	[ICM42688_DT_GYRO_ODR_200] = SENSOR_ODR_MHZ_TO_PERIOD_NS(200000),
	[ICM42688_DT_GYRO_ODR_500] = SENSOR_ODR_MHZ_TO_PERIOD_NS(500000),
	[ICM42688_DT_GYRO_ODR_1000] = SENSOR_ODR_MHZ_TO_PERIOD_NS(1000000),
	[ICM42688_DT_GYRO_ODR_2000] = SENSOR_ODR_MHZ_TO_PERIOD_NS(2000000),
	[ICM42688_DT_GYRO_ODR_4000] = SENSOR_ODR_MHZ_TO_PERIOD_NS(4000000),
	[ICM42688_DT_GYRO_ODR_8000] = SENSOR_ODR_MHZ_TO_PERIOD_NS(8000000),
	[ICM42688_DT_GYRO_ODR_16000] = SENSOR_ODR_MHZ_TO_PERIOD_NS(16000000),
	[ICM42688_DT_GYRO_ODR_32000] = SENSOR_ODR_MHZ_TO_PERIOD_NS(32000000),
};

/* Nominal clock of the ODR settings; with an external clock of f Hz, the ODR scales by f/32000 */
#define ICM4268X_NOMINAL_RTC_FREQ 32000U

/* Channel to decode: sensor and axes */
struct icm4268x_chan {
	enum icm4268x_sensor sensor;
	uint8_t first_axis;
	uint8_t num_axes;
};

/* Description of the frames passed to icm4268x_decode_frame() */
struct icm4268x_frame_format {
	const struct alignment *axis_align;
	bool is_fifo;
	/* One LSB of an accelerometer or gyroscope sample: num / den of its unit at scale_shift */
	int32_t num;
	int32_t den;
	int8_t scale_shift;
};

static int icm4268x_chan_get(enum sensor_channel chan_type, struct icm4268x_chan *chan)
{
	switch (chan_type) {
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z:
		*chan = (struct icm4268x_chan){ICM4268X_SENSOR_ACCEL,
					       (uint8_t)(chan_type - SENSOR_CHAN_ACCEL_X), 1U};
		return 0;
	case SENSOR_CHAN_ACCEL_XYZ:
		*chan = (struct icm4268x_chan){ICM4268X_SENSOR_ACCEL, 0U, 3U};
		return 0;
	case SENSOR_CHAN_GYRO_X:
	case SENSOR_CHAN_GYRO_Y:
	case SENSOR_CHAN_GYRO_Z:
		*chan = (struct icm4268x_chan){ICM4268X_SENSOR_GYRO,
					       (uint8_t)(chan_type - SENSOR_CHAN_GYRO_X), 1U};
		return 0;
	case SENSOR_CHAN_GYRO_XYZ:
		*chan = (struct icm4268x_chan){ICM4268X_SENSOR_GYRO, 0U, 3U};
		return 0;
	case SENSOR_CHAN_DIE_TEMP:
		*chan = (struct icm4268x_chan){ICM4268X_SENSOR_TEMP, 0U, 1U};
		return 0;
	default:
		return -ENOTSUP;
	}
}

static bool icm4268x_fifo_packet_has(const uint8_t *packet, enum icm4268x_sensor sensor)
{
	const bool has_accel = FIELD_GET(FIFO_HEADER_ACCEL, packet[0]) != 0U;
	const bool has_gyro = FIELD_GET(FIFO_HEADER_GYRO, packet[0]) != 0U;

	switch (sensor) {
	case ICM4268X_SENSOR_ACCEL:
		return has_accel;
	case ICM4268X_SENSOR_GYRO:
		return has_gyro;
	default:
		return has_accel || has_gyro;
	}
}

static int icm4268x_fifo_packet_len(const uint8_t *packet, size_t remaining, const void *user_data)
{
	ARG_UNUSED(remaining);
	ARG_UNUSED(user_data);

	if (FIELD_GET(FIFO_HEADER_20, packet[0]) != 0U) {
		return ICM4268X_FIFO_PACKET_20_SIZE;
	}

	if (FIELD_GET(FIFO_HEADER_ACCEL, packet[0]) != 0U &&
	    FIELD_GET(FIFO_HEADER_GYRO, packet[0]) != 0U) {
		return ICM4268X_FIFO_PACKET_16_SIZE;
	}

	return ICM4268X_FIFO_PACKET_8_SIZE;
}

static q31_t icm4268x_temp_to_q31(int32_t raw, int32_t sensitivity100)
{
	/* (raw / (sensitivity100 / 100)) + 25 C */
	return sensor_raw_to_q31_ratio(
		(uint32_t)(raw * 100 + ICM4268X_TEMP_OFFSET_C * sensitivity100), 32U, 1,
		sensitivity100, ICM4268X_TEMP_SHIFT);
}

static q31_t icm4268x_decode_temp(const uint8_t *frame, const struct icm4268x_frame_format *fmt)
{
	int32_t raw;

	if (!fmt->is_fifo) {
		raw = (int16_t)sys_get_be16(&frame[ICM4268X_ONE_SHOT_TEMP_OFFSET]);
		return icm4268x_temp_to_q31(raw, ICM4268X_TEMP_SENS_16);
	}

	if (FIELD_GET(FIFO_HEADER_20, frame[0]) != 0U) {
		raw = (int16_t)sys_get_be16(&frame[ICM4268X_FIFO_TEMP_OFFSET_16]);
		return icm4268x_temp_to_q31(raw, ICM4268X_TEMP_SENS_16);
	}

	if (FIELD_GET(FIFO_HEADER_ACCEL, frame[0]) != 0U &&
	    FIELD_GET(FIFO_HEADER_GYRO, frame[0]) != 0U) {
		raw = (int8_t)frame[ICM4268X_FIFO_TEMP_OFFSET_16];
	} else {
		raw = (int8_t)frame[ICM4268X_FIFO_TEMP_OFFSET_8];
	}

	return icm4268x_temp_to_q31(raw, ICM4268X_TEMP_SENS_8);
}

static q31_t icm4268x_ratio_to_q31(int32_t raw, int32_t num, int32_t den, int8_t shift)
{
	/* A negative shift is folded into the ratio */
	if (shift < 0) {
		return sensor_raw_to_q31_ratio((uint32_t)raw, 32U, num * (1 << -shift), den, 0);
	}

	return sensor_raw_to_q31_ratio((uint32_t)raw, 32U, num, den, shift);
}

static int icm4268x_decode_axis(const uint8_t *frame, const struct icm4268x_frame_format *fmt,
				enum icm4268x_sensor sensor, uint8_t axis, q31_t *out)
{
	const struct alignment *align = &fmt->axis_align[axis];
	const bool is_gyro = sensor == ICM4268X_SENSOR_GYRO;
	uint8_t hw_axis = (uint8_t)align->index;
	bool is_hires;
	size_t offset;
	int32_t raw;

	if (!fmt->is_fifo) {
		offset =
			(is_gyro ? ICM4268X_ONE_SHOT_GYRO_OFFSET : ICM4268X_ONE_SHOT_ACCEL_OFFSET) +
			hw_axis * 2U;
		raw = (int16_t)sys_get_be16(&frame[offset]);
		*out = icm4268x_ratio_to_q31(align->sign * raw, fmt->num, fmt->den,
					     fmt->scale_shift);

		return 0;
	}

	is_hires = FIELD_GET(FIFO_HEADER_20, frame[0]) != 0U;
	offset = ICM4268X_FIFO_DATA_OFFSET + hw_axis * 2U;
	if (is_gyro && FIELD_GET(FIFO_HEADER_ACCEL, frame[0]) != 0U) {
		offset += 6U;
	}

	if (is_hires) {
		uint8_t ext = frame[ICM4268X_FIFO_EXTENSION_OFFSET + hw_axis];
		uint32_t low = is_gyro ? FIELD_GET(ICM4268X_FIFO_EXTENSION_GYRO, ext)
				       : FIELD_GET(ICM4268X_FIFO_EXTENSION_ACCEL, ext);

		raw = sign_extend(((uint32_t)sys_get_be16(&frame[offset]) << 4) | low, 19U);

		/*
		 * FIFO_HOLD_LAST_DATA_EN is left at 0 in INTF_CONFIG0, so -524288 marks an
		 * invalid 20-bit sample.
		 */
		if (raw == ICM4268X_FIFO_20_BITS_INVALID) {
			return -ENODATA;
		}
	} else {
		raw = (int16_t)sys_get_be16(&frame[offset]);
	}

	*out = icm4268x_ratio_to_q31(align->sign * raw, fmt->num, fmt->den,
				     is_hires ? fmt->scale_shift + ICM4268X_FIFO_20_BITS_EXTRA_SHIFT
					      : fmt->scale_shift);

	return 0;
}

static int icm4268x_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				 const void *user_data, struct sensor_frame_reading *reading)
{
	const struct icm4268x_frame_format *fmt = user_data;
	struct icm4268x_chan chan;
	int rc;

	rc = icm4268x_chan_get(chan_spec.chan_type, &chan);
	if (rc != 0) {
		return rc;
	}

	if (fmt->is_fifo && !icm4268x_fifo_packet_has(frame, chan.sensor)) {
		return 0;
	}

	if (reading == NULL) {
		return 1;
	}

	if (chan.sensor == ICM4268X_SENSOR_TEMP) {
		reading->values[0] = icm4268x_decode_temp(frame, fmt);
		return 1;
	}

	for (uint8_t i = 0U; i < chan.num_axes; i++) {
		rc = icm4268x_decode_axis(frame, fmt, chan.sensor, chan.first_axis + i,
					  &reading->values[i]);
		if (rc != 0) {
			return rc;
		}
	}

	return 1;
}

/* Set the value of one LSB of the one-shot data from the sensitivity at the full scale */
static int icm4268x_one_shot_scale(const struct icm4268x_decoder_header *header,
				   enum icm4268x_sensor sensor, struct icm4268x_frame_format *fmt)
{
	const enum icm4268x_variant variant = header->variant;

	if (sensor == ICM4268X_SENSOR_ACCEL) {
		for (uint8_t i = 0U; i < table_accel_fs_to_reg_array_size[variant]; i++) {
			if (header->accel_fs == table_accel_sensitivity_to_reg[variant][i].reg) {
				/* m/s^2: SENSOR_G / (1000000 * LSB/g) */
				fmt->num = SENSOR_G / 10;
				fmt->den = table_accel_sensitivity_to_reg[variant][i].val * 100000;
				return 0;
			}
		}
	} else if (sensor == ICM4268X_SENSOR_GYRO) {
		for (uint8_t i = 0U; i < table_gyro_fs_to_reg_array_size[variant]; i++) {
			if (header->gyro_fs == table_gyro_sensitivity_to_reg[variant][i].reg) {
				/* rad/s: pi / (180 * LSB/dps), sensitivity in tenths of LSB/dps */
				fmt->num = ICM4268X_PI_NUM * 10;
				fmt->den = ICM4268X_PI_DEN * 180 *
					   table_gyro_sensitivity_to_reg[variant][i].val;
				return 0;
			}
		}
	} else {
		return 0;
	}

	return -EINVAL;
}

static int icm4268x_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
			       struct sensor_raw_frames *frames, struct icm4268x_frame_format *fmt)
{
	const struct icm4268x_decoder_header *header =
		(const struct icm4268x_decoder_header *)buffer;
	struct icm4268x_chan chan;
	int8_t shift;
	int rc;

	rc = icm4268x_chan_get(chan_spec.chan_type, &chan);
	if (rc != 0 || chan_spec.chan_idx != 0U) {
		return -ENOTSUP;
	}

	for (uint8_t i = 0U; i < ARRAY_SIZE(header->axis_align); i++) {
		if (header->axis_align[i].index < 0 || header->axis_align[i].index > 2) {
			return -EINVAL;
		}
	}

	rc = icm4268x_get_shift(chan_spec.chan_type, header->accel_fs, header->gyro_fs,
				header->variant, &shift);
	if (rc != 0) {
		return rc;
	}

	*fmt = (struct icm4268x_frame_format){
		.axis_align = header->axis_align,
		.is_fifo = header->is_fifo == 1U,
		.scale_shift = shift,
	};
	*frames = (struct sensor_raw_frames){
		.decode_frame = icm4268x_decode_frame,
		.user_data = fmt,
		.timestamp_ns = header->timestamp,
		.shift = shift,
	};

	if (fmt->is_fifo) {
		const struct icm4268x_fifo_data *fdata = (const struct icm4268x_fifo_data *)buffer;
		uint64_t period_ns = 0U;

		/* The temperature follows the accelerometer */
		if (chan.sensor == ICM4268X_SENSOR_GYRO) {
			if (fdata->gyro_odr < ARRAY_SIZE(gyro_period_ns)) {
				period_ns = gyro_period_ns[fdata->gyro_odr];
			}
		} else if (fdata->accel_odr < ARRAY_SIZE(accel_period_ns)) {
			period_ns = accel_period_ns[fdata->accel_odr];
		}

		if (period_ns == 0U || fdata->rtc_freq == 0U) {
			return -EINVAL;
		}

		/* The header timestamp is the one of the newest packet */
		frames->frames = buffer + sizeof(*fdata);
		frames->size = fdata->fifo_count;
		frames->frame_len = icm4268x_fifo_packet_len;
		frames->period_ns = period_ns * ICM4268X_NOMINAL_RTC_FREQ;
		frames->period_den = fdata->rtc_freq;

		if (chan.sensor != ICM4268X_SENSOR_TEMP) {
			fmt->num = fifo_scale[chan.sensor].num;
			fmt->den = fifo_scale[chan.sensor].den;
			fmt->scale_shift = fifo_scale[chan.sensor].shift;
		}
	} else {
		const struct icm4268x_encoded_data *edata =
			(const struct icm4268x_encoded_data *)buffer;
		const uint8_t channel_request = icm4268x_encode_channel(chan_spec.chan_type);

		if ((edata->channels & channel_request) != channel_request) {
			return -ENODATA;
		}

		frames->frames = edata->readings;
		frames->size = sizeof(edata->readings);
		frames->frame_size = sizeof(edata->readings);

		rc = icm4268x_one_shot_scale(header, chan.sensor, fmt);
		if (rc != 0) {
			return rc;
		}
	}

	return 0;
}

static int icm4268x_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				   uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct icm4268x_frame_format fmt;
	struct sensor_raw_frames frames;
	int rc;

	rc = icm4268x_get_frames(buffer, chan_spec, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

static int icm4268x_decoder_get_frame_count(const uint8_t *buffer,
					    struct sensor_chan_spec chan_spec,
					    uint16_t *frame_count)
{
	struct icm4268x_frame_format fmt;
	struct sensor_raw_frames frames;
	int rc;

	rc = icm4268x_get_frames(buffer, chan_spec, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int icm4268x_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					  size_t *frame_size)
{
	struct icm4268x_chan chan;

	if (icm4268x_chan_get(chan_spec.chan_type, &chan) != 0) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 0U, base_size, frame_size);
}

static bool icm4268x_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	const struct icm4268x_fifo_data *edata = (const struct icm4268x_fifo_data *)buffer;

	if (edata->header.is_fifo == 0U) {
		return false;
	}

	switch (trigger) {
	case SENSOR_TRIG_DATA_READY:
		return FIELD_GET(BIT_DATA_RDY_INT, edata->int_status) != 0U;
	case SENSOR_TRIG_FIFO_WATERMARK:
		return FIELD_GET(BIT_FIFO_THS_INT, edata->int_status) != 0U;
	case SENSOR_TRIG_FIFO_FULL:
		return FIELD_GET(BIT_FIFO_FULL_INT, edata->int_status) != 0U;
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
