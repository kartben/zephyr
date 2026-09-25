/*
 * Copyright (c) 2023 Google LLC
 * Copyright (c) 2025 Croxel Inc.
 * Copyright (c) 2025 CogniPilot Foundation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

#include <zephyr/drivers/sensor_clock.h>
#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/sys/byteorder.h>

#include "icm566xx.h"
#include "icm566xx_decoder.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ICM566XX_DECODER, CONFIG_SENSOR_LOG_LEVEL);

/*
 * ICM56686 register data has 20 bits: the upper 16 bits are in the data registers, the lower 4
 * bits in EXT_DATA_X/Y/Z, which follow the 14 bytes of data registers in the encoded payload.
 * Its shifts keep 4 more bits of headroom than the ICM56622 ones.
 */
#define ICM566XX_HIGH_FSR        IS_ENABLED(CONFIG_DT_HAS_INVENSENSE_ICM56686_ENABLED)
#define ICM566XX_REG_DATA_BITS   (ICM566XX_HIGH_FSR ? 20U : 16U)
#define ICM566XX_SHIFT_HEADROOM  (ICM566XX_HIGH_FSR ? 4 : 0)
#define ICM566XX_EXT_DATA_OFFSET sizeof(((const struct icm566xx_encoded_payload *)NULL)->buf)

/* High resolution FIFO frames have 20-bit samples at the highest full scale of the part */
#define ICM566XX_FIFO_BITS 20U
#define ICM566XX_FIFO_ACCEL_FS                                                                     \
	(ICM566XX_HIGH_FSR ? ICM566XX_DT_ACCEL_FS_32 : ICM566XX_DT_ACCEL_FS_16)
#define ICM566XX_FIFO_GYRO_FS                                                                      \
	(ICM566XX_HIGH_FSR ? ICM566XX_DT_GYRO_FS_4000 : ICM566XX_DT_GYRO_FS_2000)
#define ICM566XX_FIFO_DATA_OFFSET offsetof(struct icm566xx_encoded_fifo_payload, accel)
#define ICM566XX_FIFO_LSB_OFFSET  offsetof(struct icm566xx_encoded_fifo_payload, lsb)

/* Temperature: 132.48 LSB/degC, 0 LSB at 25 degC */
#define ICM566XX_TEMP_OFFSET_LSB 3312
#define ICM566XX_TEMP_SHIFT      9

/* Full scale of the accelerometer at ICM566XX_DT_ACCEL_FS_32, in g, as a power of two */
#define ICM566XX_ACCEL_FS_32_LOG2 5

/* 4000 dps in rad/s, the gyroscope full scale at ICM566XX_DT_GYRO_FS_4000 */
#define ICM566XX_GYRO_FS_4000_NUM ((int32_t)(200 * SENSOR_PI))
#define ICM566XX_GYRO_FS_4000_DEN 9000000

/*
 * Conversion of the samples of one channel: offset is added to the sign-extended sample of the
 * given bits, and one LSB is num / den / 2^lsb_exp. The power of two is applied with the shift
 * given to sensor_raw_to_q31_ratio(), and with den when that shift would exceed 31.
 */
struct icm566xx_frame_format {
	bool fifo;
	uint8_t bits;
	int32_t offset;
	int32_t num;
	int32_t den;
	int8_t ratio_shift;
};

static int icm566xx_get_channel_position(enum sensor_channel chan)
{
	switch (chan) {
	case SENSOR_CHAN_ACCEL_XYZ:
	case SENSOR_CHAN_ACCEL_X:
		return 0;
	case SENSOR_CHAN_ACCEL_Y:
		return 1;
	case SENSOR_CHAN_ACCEL_Z:
		return 2;
	case SENSOR_CHAN_GYRO_XYZ:
	case SENSOR_CHAN_GYRO_X:
		return 3;
	case SENSOR_CHAN_GYRO_Y:
		return 4;
	case SENSOR_CHAN_GYRO_Z:
		return 5;
	case SENSOR_CHAN_DIE_TEMP:
		return 6;
	default:
		return 0;
	}
}

static uint8_t icm566xx_encode_channel(enum sensor_channel chan)
{
	uint8_t encode_bmask = 0;

	switch (chan) {
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z:
	case SENSOR_CHAN_GYRO_X:
	case SENSOR_CHAN_GYRO_Y:
	case SENSOR_CHAN_GYRO_Z:
	case SENSOR_CHAN_DIE_TEMP:
		encode_bmask = BIT(icm566xx_get_channel_position(chan));
		break;
	case SENSOR_CHAN_ACCEL_XYZ:
		encode_bmask = BIT(icm566xx_get_channel_position(SENSOR_CHAN_ACCEL_X)) |
			       BIT(icm566xx_get_channel_position(SENSOR_CHAN_ACCEL_Y)) |
			       BIT(icm566xx_get_channel_position(SENSOR_CHAN_ACCEL_Z));
		break;
	case SENSOR_CHAN_GYRO_XYZ:
		encode_bmask = BIT(icm566xx_get_channel_position(SENSOR_CHAN_GYRO_X)) |
			       BIT(icm566xx_get_channel_position(SENSOR_CHAN_GYRO_Y)) |
			       BIT(icm566xx_get_channel_position(SENSOR_CHAN_GYRO_Z));
		break;
	default:
		break;
	}

	return encode_bmask;
}

int icm566xx_encode(const struct device *dev, const struct sensor_chan_spec *const channels,
		    const size_t num_channels, uint8_t *buf)
{
	struct icm566xx_encoded_data *edata = (struct icm566xx_encoded_data *)buf;
	const struct icm566xx_data *data = dev->data;
	uint64_t cycles;
	int err;

	edata->header.channels = 0;

	for (size_t i = 0; i < num_channels; i++) {
		edata->header.channels |= icm566xx_encode_channel(channels[i].chan_type);
	}

	err = sensor_clock_get_cycles(&cycles);
	if (err != 0) {
		return err;
	}

	edata->header.events = 0;
	edata->header.accel_fs = data->edata.header.accel_fs;
	edata->header.gyro_fs = data->edata.header.gyro_fs;
	edata->header.timestamp = sensor_clock_cycles_to_ns(cycles);

	return 0;
}

static bool icm566xx_is_fifo(const struct icm566xx_encoded_data *edata)
{
	uint8_t ev_raw = edata->header.events;
	encoded_events_t ev_status = *(encoded_events_t *)&ev_raw;

	return ev_status.int1_status_fifo_ths != 0U || ev_status.int1_status_fifo_full != 0U;
}

static bool icm566xx_chan_is_supported(struct sensor_chan_spec chan_spec, bool fifo)
{
	if (chan_spec.chan_idx != 0U) {
		return false;
	}

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_ACCEL_XYZ:
	case SENSOR_CHAN_GYRO_XYZ:
	case SENSOR_CHAN_DIE_TEMP:
		return true;
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z:
	case SENSOR_CHAN_GYRO_X:
	case SENSOR_CHAN_GYRO_Y:
	case SENSOR_CHAN_GYRO_Z:
		/* Single axes are not decoded from FIFO frames */
		return !fifo;
	default:
		return false;
	}
}

static uint32_t icm566xx_raw_get(const uint8_t *frame, const struct icm566xx_frame_format *fmt,
				 uint8_t pos)
{
	const uint8_t *data = fmt->fifo ? &frame[ICM566XX_FIFO_DATA_OFFSET] : frame;
	uint32_t raw = sys_get_le16(&data[pos * 2U]);
	uint8_t axis = pos % 3U;
	bool accel = pos < 3U;
	uint8_t lsb;

	if (fmt->bits != 20U) {
		return raw;
	}

	/* FIFO frames carry the accel LSBs in the upper nibble, EXT_DATA in the lower one */
	if (fmt->fifo) {
		lsb = frame[ICM566XX_FIFO_LSB_OFFSET + axis];
		lsb = accel ? (lsb >> 4) : (lsb & 0x0FU);
	} else {
		lsb = frame[ICM566XX_EXT_DATA_OFFSET + axis];
		lsb = accel ? (lsb & 0x0FU) : (lsb >> 4);
	}

	return (raw << 4) | lsb;
}

static int icm566xx_fifo_frame_check(const uint8_t *frame, uint8_t first, uint8_t num)
{
	const fifo_header_t header = {.Byte = frame[0]};

	/* Only 20-byte frames with accel and gyro data, and no auxiliary sensors, are supported */
	if (header.bits.ext_header != 0U || header.bits.accel_bit == 0U ||
	    header.bits.gyro_bit == 0U || header.bits.twentybits_bit == 0U) {
		LOG_ERR("Unsupported FIFO packet format 0x%02x", header.Byte);
		return -ENOTSUP;
	}

	/* A frame without data for the channel holds no reading */
	for (uint8_t i = 0U; i < num; i++) {
		if (sys_get_le16(&frame[ICM566XX_FIFO_DATA_OFFSET + (first + i) * 2U]) ==
		    FIFO_NO_DATA) {
			return 0;
		}
	}

	return 1;
}

static int icm566xx_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				 const void *user_data, struct sensor_frame_reading *reading)
{
	const struct icm566xx_frame_format *fmt = user_data;
	const uint8_t first = icm566xx_get_channel_position(chan_spec.chan_type);
	const uint8_t num = SENSOR_CHANNEL_3_AXIS(chan_spec.chan_type) ? 3U : 1U;
	int rc;

	if (fmt->fifo) {
		rc = icm566xx_fifo_frame_check(frame, first, num);
		if (rc <= 0) {
			return rc;
		}
	}

	if (reading == NULL) {
		return 1;
	}

	for (uint8_t i = 0U; i < num; i++) {
		int32_t sample =
			sign_extend(icm566xx_raw_get(frame, fmt, first + i), fmt->bits - 1U) +
			fmt->offset;

		reading->values[i] = sensor_raw_to_q31_ratio((uint32_t)sample, 32U, fmt->num,
							     fmt->den, fmt->ratio_shift);
	}

	return 1;
}

static int icm566xx_format_set(enum sensor_channel chan, uint8_t accel_fs, uint8_t gyro_fs,
			       uint8_t bits, struct icm566xx_frame_format *fmt, int8_t *shift)
{
	uint8_t lsb_exp;
	int8_t exp;

	fmt->bits = bits;
	fmt->offset = 0;

	/* Each halving of the full scale takes one bit off the shift and adds one to lsb_exp */
	if (SENSOR_CHANNEL_IS_ACCEL(chan)) {
		if (accel_fs > ICM566XX_DT_ACCEL_FS_2) {
			return -EINVAL;
		}

		*shift = 9 - accel_fs + ICM566XX_SHIFT_HEADROOM;
		fmt->num = SENSOR_G;
		fmt->den = 1000000;
		lsb_exp = bits - 1U - ICM566XX_ACCEL_FS_32_LOG2 + accel_fs;
	} else if (SENSOR_CHANNEL_IS_GYRO(chan)) {
		if (gyro_fs > ICM566XX_DT_GYRO_FS_15_625) {
			return -EINVAL;
		}

		*shift = 12 - gyro_fs + ICM566XX_SHIFT_HEADROOM;
		fmt->num = ICM566XX_GYRO_FS_4000_NUM;
		fmt->den = ICM566XX_GYRO_FS_4000_DEN;
		lsb_exp = bits - 1U + gyro_fs;
	} else {
		*shift = ICM566XX_TEMP_SHIFT;
		fmt->bits = 16U;
		fmt->offset = ICM566XX_TEMP_OFFSET_LSB;
		fmt->num = 100;
		fmt->den = 13248;
		lsb_exp = 0U;
	}

	exp = *shift + (int8_t)lsb_exp;
	fmt->ratio_shift = MIN(exp, 31);
	fmt->den *= (int32_t)BIT(exp - fmt->ratio_shift);

	return 0;
}

static int icm566xx_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
			       struct sensor_raw_frames *frames, struct icm566xx_frame_format *fmt)
{
	const struct icm566xx_encoded_data *edata = (const struct icm566xx_encoded_data *)buffer;
	const uint8_t channel_request = icm566xx_encode_channel(chan_spec.chan_type);
	uint8_t accel_fs;
	uint8_t gyro_fs;
	uint8_t bits;

	fmt->fifo = icm566xx_is_fifo(edata);

	if (!icm566xx_chan_is_supported(chan_spec, fmt->fifo)) {
		return -ENOTSUP;
	}

	if ((edata->header.channels & channel_request) != channel_request) {
		return -ENODATA;
	}

	*frames = (struct sensor_raw_frames){
		.decode_frame = icm566xx_decode_frame,
		.user_data = fmt,
		.timestamp_ns = edata->header.timestamp,
	};

	if (fmt->fifo) {
		/* All frames are timestamped at the interrupt that triggered the read */
		frames->frames = (const uint8_t *)edata->fifo_payload;
		frames->frame_size = sizeof(struct icm566xx_encoded_fifo_payload);
		frames->size = edata->header.fifo_count * frames->frame_size;
		accel_fs = ICM566XX_FIFO_ACCEL_FS;
		gyro_fs = ICM566XX_FIFO_GYRO_FS;
		bits = ICM566XX_FIFO_BITS;
	} else {
		frames->frames = (const uint8_t *)&edata->payload;
		frames->frame_size = sizeof(edata->payload);
		frames->size = sizeof(edata->payload);
		accel_fs = edata->header.accel_fs;
		gyro_fs = edata->header.gyro_fs;
		bits = ICM566XX_REG_DATA_BITS;
	}

	return icm566xx_format_set(chan_spec.chan_type, accel_fs, gyro_fs, bits, fmt,
				   &frames->shift);
}

static int icm566xx_decoder_get_frame_count(const uint8_t *buffer,
					    struct sensor_chan_spec chan_spec,
					    uint16_t *frame_count)
{
	struct sensor_raw_frames frames;
	struct icm566xx_frame_format fmt;
	int rc;

	rc = icm566xx_get_frames(buffer, chan_spec, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int icm566xx_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					  size_t *frame_size)
{
	/* Sizes do not depend on the channel index */
	chan_spec.chan_idx = 0U;

	if (!icm566xx_chan_is_supported(chan_spec, false)) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 0U, base_size, frame_size);
}

static int icm566xx_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				   uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_raw_frames frames;
	struct icm566xx_frame_format fmt;
	int rc;

	rc = icm566xx_get_frames(buffer, chan_spec, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

static bool icm566xx_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	struct icm566xx_encoded_data *edata = (struct icm566xx_encoded_data *)buffer;
	uint8_t ev_raw = edata->header.events;
	encoded_events_t ev_status = *(encoded_events_t *)&ev_raw;

	switch (trigger) {
	case SENSOR_TRIG_DATA_READY:
		return ev_status.int1_status_drdy != 0U;
	case SENSOR_TRIG_FIFO_WATERMARK:
		return ev_status.int1_status_fifo_ths != 0U;
	case SENSOR_TRIG_FIFO_FULL:
		return ev_status.int1_status_fifo_full != 0U;
	default:
		break;
	}

	return false;
}

/* One decoder per compatible, so that SENSOR_DECODER_DT_GET() works for both parts */
#define ICM566XX_DECODER_API                                                                       \
	{                                                                                          \
		.get_frame_count = icm566xx_decoder_get_frame_count,                               \
		.get_size_info = icm566xx_decoder_get_size_info,                                   \
		.decode = icm566xx_decoder_decode,                                                 \
		.has_trigger = icm566xx_decoder_has_trigger,                                       \
	}

#define DT_DRV_COMPAT invensense_icm56622
SENSOR_DECODER_API_DT_DEFINE() = ICM566XX_DECODER_API;

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT invensense_icm56686
SENSOR_DECODER_API_DT_DEFINE() = ICM566XX_DECODER_API;

int icm566xx_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
