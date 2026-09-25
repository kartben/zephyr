/*
 * Copyright (c) 2023 Google LLC
 * Copyright (c) 2025 Croxel Inc.
 * Copyright (c) 2025 CogniPilot Foundation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor_clock.h>
#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/check.h>

#include "icm45686.h"
#include "icm45686_reg.h"
#include "icm45686_decoder.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ICM45686_DECODER, CONFIG_SENSOR_LOG_LEVEL);

#define DT_DRV_COMPAT invensense_icm45686

/* Hardware timestamp resolution: 16 us per tick (TMST_RESOL=1 in TMST_WOM_CONFIG) */
#define ICM45686_HW_TS_NS_PER_TICK UINT64_C(16000)

/*
 * Positions of the samples in the single sample payload (int16_t words) and in the FIFO
 * packets (16-bit MSB words after the packet header)
 */
#define ICM45686_POS_ACCEL 0U
#define ICM45686_POS_GYRO  3U
#define ICM45686_POS_TEMP  6U

#define ICM45686_FIFO_DATA_OFFSET offsetof(struct icm45686_encoded_fifo_payload, accel.x)
#define ICM45686_FIFO_TS_OFFSET   offsetof(struct icm45686_encoded_fifo_payload, timestamp)
#define ICM45686_FIFO_LSB_OFFSET  offsetof(struct icm45686_encoded_fifo_payload, lsb)

/* Temperature: 132.48 LSB/degC, 0 LSB at 25 degC */
#define ICM45686_TEMP_SHIFT        9
#define ICM45686_TEMP_LSB_PER_100C 13248
#define ICM45686_TEMP_OFFSET_Q31   SENSOR_Q31_SCALE(25, 1, ICM45686_TEMP_SHIFT)

/* Full scale in micro-rad/s from a full scale in milli-degrees/s */
#define ICM45686_GYRO_FS_URADS(fs_mdps)                                                            \
	((int32_t)(((int64_t)(fs_mdps) * SENSOR_PI + 90000) / 180000))

static const int32_t accel_fs_ums2[] = {
	[ICM45686_DT_ACCEL_FS_32] = 32 * SENSOR_G,
	[ICM45686_DT_ACCEL_FS_16] = 16 * SENSOR_G,
	[ICM45686_DT_ACCEL_FS_8] = 8 * SENSOR_G,
	[ICM45686_DT_ACCEL_FS_4] = 4 * SENSOR_G,
	[ICM45686_DT_ACCEL_FS_2] = 2 * SENSOR_G,
};

static const int8_t accel_shift[] = {
	[ICM45686_DT_ACCEL_FS_32] = 9,
	[ICM45686_DT_ACCEL_FS_16] = 8,
	[ICM45686_DT_ACCEL_FS_8] = 7,
	[ICM45686_DT_ACCEL_FS_4] = 6,
	[ICM45686_DT_ACCEL_FS_2] = 5,
};

static const int32_t gyro_fs_urads[] = {
	[ICM45686_DT_GYRO_FS_4000] = ICM45686_GYRO_FS_URADS(4000000),
	[ICM45686_DT_GYRO_FS_2000] = ICM45686_GYRO_FS_URADS(2000000),
	[ICM45686_DT_GYRO_FS_1000] = ICM45686_GYRO_FS_URADS(1000000),
	[ICM45686_DT_GYRO_FS_500] = ICM45686_GYRO_FS_URADS(500000),
	[ICM45686_DT_GYRO_FS_250] = ICM45686_GYRO_FS_URADS(250000),
	[ICM45686_DT_GYRO_FS_125] = ICM45686_GYRO_FS_URADS(125000),
	[ICM45686_DT_GYRO_FS_62_5] = ICM45686_GYRO_FS_URADS(62500),
	[ICM45686_DT_GYRO_FS_31_25] = ICM45686_GYRO_FS_URADS(31250),
	[ICM45686_DT_GYRO_FS_15_625] = ICM45686_GYRO_FS_URADS(15625),
};

static const int8_t gyro_shift[] = {
	[ICM45686_DT_GYRO_FS_4000] = 12,
	[ICM45686_DT_GYRO_FS_2000] = 11,
	[ICM45686_DT_GYRO_FS_1000] = 10,
	[ICM45686_DT_GYRO_FS_500] = 9,
	[ICM45686_DT_GYRO_FS_250] = 8,
	[ICM45686_DT_GYRO_FS_125] = 7,
	[ICM45686_DT_GYRO_FS_62_5] = 6,
	[ICM45686_DT_GYRO_FS_31_25] = 5,
	[ICM45686_DT_GYRO_FS_15_625] = 4,
};

struct icm45686_frame_format {
	/* 20-bit FIFO packets instead of one 16-bit sample of each channel */
	bool fifo;
	/* Full scale of the decoded channel in micro-units, 0 for the temperature */
	int32_t fs;
	int8_t shift;
	/* Host timestamp and hardware timestamp of the last FIFO packet */
	uint64_t timestamp_ns;
	uint16_t hw_ts_last;
};

static bool icm45686_is_fifo(const struct icm45686_encoded_header *header)
{
	return (header->events &
		(REG_INT1_STATUS0_FIFO_THS(true) | REG_INT1_STATUS0_FIFO_FULL(true))) != 0U;
}

/* Get the position of the first sample of a channel and the number of samples */
static int icm45686_chan_layout(enum sensor_channel chan, uint8_t *pos, uint8_t *num)
{
	*num = 1U;

	switch (chan) {
	case SENSOR_CHAN_ACCEL_XYZ:
		*num = 3U;
		__fallthrough;
	case SENSOR_CHAN_ACCEL_X:
		*pos = ICM45686_POS_ACCEL;
		return 0;
	case SENSOR_CHAN_ACCEL_Y:
		*pos = ICM45686_POS_ACCEL + 1U;
		return 0;
	case SENSOR_CHAN_ACCEL_Z:
		*pos = ICM45686_POS_ACCEL + 2U;
		return 0;
	case SENSOR_CHAN_GYRO_XYZ:
		*num = 3U;
		__fallthrough;
	case SENSOR_CHAN_GYRO_X:
		*pos = ICM45686_POS_GYRO;
		return 0;
	case SENSOR_CHAN_GYRO_Y:
		*pos = ICM45686_POS_GYRO + 1U;
		return 0;
	case SENSOR_CHAN_GYRO_Z:
		*pos = ICM45686_POS_GYRO + 2U;
		return 0;
	case SENSOR_CHAN_DIE_TEMP:
		*pos = ICM45686_POS_TEMP;
		return 0;
	default:
		return -ENOTSUP;
	}
}

static uint8_t icm45686_encode_channel(enum sensor_channel chan)
{
	uint8_t pos;
	uint8_t num;

	if (icm45686_chan_layout(chan, &pos, &num) != 0) {
		return 0U;
	}

	return (uint8_t)(BIT_MASK(num) << pos);
}

/* Convert a sample whose full scale, reached at 2^(bits - 1) LSB, is fs micro-units */
static q31_t icm45686_convert(uint32_t raw, uint8_t bits, const struct icm45686_frame_format *fmt)
{
	if (fmt->fs == 0) {
		return sensor_raw_to_q31_ratio(raw, bits, 100, ICM45686_TEMP_LSB_PER_100C,
					       ICM45686_TEMP_SHIFT) +
		       ICM45686_TEMP_OFFSET_Q31;
	}

	/* One LSB is fs / 2^(bits - 1): dividing by 2^(bits - 1) adds bits - 1 to the shift */
	return sensor_raw_to_q31_ratio(raw, bits, fmt->fs, 1000000,
				       (int8_t)(fmt->shift + bits - 1));
}

static int icm45686_decode_fifo_frame(const uint8_t *frame, uint8_t pos, uint8_t num,
				      const struct icm45686_frame_format *fmt,
				      struct sensor_frame_reading *reading)
{
	const uint8_t header = frame[0];
	uint16_t hw_ts;

	/* This driver assumes 20-byte packets with both accel and gyro and no auxiliary sensor */
	CHECKIF(!(((header & FIFO_HEADER_EXT_HEADER_EN(true)) == 0U) &&
		  ((header & FIFO_HEADER_ACCEL_EN(true)) != 0U) &&
		  ((header & FIFO_HEADER_GYRO_EN(true)) != 0U) &&
		  ((header & FIFO_HEADER_HIRES_EN(true)) != 0U))) {
		LOG_ERR("Unsupported FIFO packet format 0x%02x", header);
		return -ENOTSUP;
	}

	for (uint8_t i = 0U; i < num; i++) {
		const uint8_t p = pos + i;
		uint32_t raw = sys_get_le16(&frame[ICM45686_FIFO_DATA_OFFSET + p * 2U]);
		uint8_t lsb;

		if (p == ICM45686_POS_TEMP) {
			reading->values[i] = icm45686_convert(raw, 16U, fmt);
			continue;
		}

		if (raw == FIFO_NO_DATA) {
			return -ENODATA;
		}

		/* Low nibbles of the 20-bit samples: accel in bits 7:4, gyro in bits 3:0 */
		if (p < ICM45686_POS_GYRO) {
			lsb = FIELD_GET(GENMASK(7, 4), frame[ICM45686_FIFO_LSB_OFFSET + p]);
		} else {
			lsb = FIELD_GET(GENMASK(3, 0),
					frame[ICM45686_FIFO_LSB_OFFSET + p - ICM45686_POS_GYRO]);
		}
		reading->values[i] = icm45686_convert((raw << 4) | lsb, 20U, fmt);
	}

	/* The last packet was read at the host timestamp; the counter wraps after 1.05 s */
	hw_ts = sys_get_le16(&frame[ICM45686_FIFO_TS_OFFSET]);
	reading->timestamp_ns = fmt->timestamp_ns -
				(uint16_t)(fmt->hw_ts_last - hw_ts) * ICM45686_HW_TS_NS_PER_TICK;

	return 1;
}

static int icm45686_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				 const void *user_data, struct sensor_frame_reading *reading)
{
	const struct icm45686_frame_format *fmt = user_data;
	uint8_t pos;
	uint8_t num;
	int rc;

	rc = icm45686_chan_layout(chan_spec.chan_type, &pos, &num);
	if (rc != 0) {
		return rc;
	}

	if (reading == NULL) {
		return 1;
	}

	if (fmt->fifo) {
		return icm45686_decode_fifo_frame(frame, pos, num, fmt, reading);
	}

	for (uint8_t i = 0U; i < num; i++) {
		reading->values[i] =
			icm45686_convert(sys_get_le16(&frame[(pos + i) * 2U]), 16U, fmt);
	}

	return 1;
}

static int icm45686_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
			       struct sensor_raw_frames *frames, struct icm45686_frame_format *fmt)
{
	const struct icm45686_encoded_data *edata = (const struct icm45686_encoded_data *)buffer;
	uint8_t accel_fs = edata->header.accel_fs;
	uint8_t gyro_fs = edata->header.gyro_fs;
	uint8_t pos;
	uint8_t num;
	int rc;

	if (chan_spec.chan_idx != 0U) {
		return -ENOTSUP;
	}

	rc = icm45686_chan_layout(chan_spec.chan_type, &pos, &num);
	if (rc != 0) {
		return rc;
	}

	*fmt = (struct icm45686_frame_format){
		.fifo = icm45686_is_fifo(&edata->header),
		.timestamp_ns = edata->header.timestamp,
	};
	*frames = (struct sensor_raw_frames){
		.decode_frame = icm45686_decode_frame,
		.user_data = fmt,
		.timestamp_ns = edata->header.timestamp,
	};

	if (fmt->fifo) {
		/* Individual axes are not decoded from FIFO packets */
		if (num == 1U && pos != ICM45686_POS_TEMP) {
			return -ENOTSUP;
		}

		/* High resolution FIFO packets have a fixed full scale */
		accel_fs = ICM45686_DT_ACCEL_FS_32;
		gyro_fs = ICM45686_DT_GYRO_FS_4000;
		frames->frames = (const uint8_t *)edata->fifo_payload;
		frames->frame_size = sizeof(struct icm45686_encoded_fifo_payload);
		frames->size = edata->header.fifo_count * frames->frame_size;
		if (frames->size > 0U) {
			fmt->hw_ts_last =
				sys_get_le16(&frames->frames[frames->size - frames->frame_size +
							     ICM45686_FIFO_TS_OFFSET]);
		}
	} else {
		uint8_t channels = icm45686_encode_channel(chan_spec.chan_type);

		if ((edata->header.channels & channels) != channels) {
			return -ENODATA;
		}

		frames->frames = edata->payload.buf;
		frames->frame_size = sizeof(edata->payload.buf);
		frames->size = sizeof(edata->payload.buf);
	}

	if (pos == ICM45686_POS_TEMP) {
		fmt->shift = ICM45686_TEMP_SHIFT;
	} else if (pos < ICM45686_POS_GYRO) {
		if (accel_fs >= ARRAY_SIZE(accel_shift)) {
			return -EINVAL;
		}
		fmt->fs = accel_fs_ums2[accel_fs];
		fmt->shift = accel_shift[accel_fs];
	} else {
		if (gyro_fs >= ARRAY_SIZE(gyro_shift)) {
			return -EINVAL;
		}
		fmt->fs = gyro_fs_urads[gyro_fs];
		fmt->shift = gyro_shift[gyro_fs];
	}
	frames->shift = fmt->shift;

	return 0;
}

int icm45686_encode(const struct device *dev, const struct sensor_chan_spec *const channels,
		    const size_t num_channels, uint8_t *buf)
{
	struct icm45686_encoded_data *edata = (struct icm45686_encoded_data *)buf;
	const struct icm45686_config *dev_config = dev->config;
	uint64_t cycles;
	int err;

	edata->header.channels = 0;

	for (size_t i = 0; i < num_channels; i++) {
		edata->header.channels |= icm45686_encode_channel(channels[i].chan_type);
	}

	err = sensor_clock_get_cycles(&cycles);
	if (err != 0) {
		return err;
	}

	edata->header.events = 0;
	edata->header.accel_fs = dev_config->settings.accel.fs;
	edata->header.gyro_fs = dev_config->settings.gyro.fs;
	edata->header.timestamp = sensor_clock_cycles_to_ns(cycles);

	return 0;
}

static int icm45686_decoder_get_frame_count(const uint8_t *buffer,
					    struct sensor_chan_spec chan_spec,
					    uint16_t *frame_count)
{
	struct icm45686_frame_format fmt;
	struct sensor_raw_frames frames;
	int rc;

	rc = icm45686_get_frames(buffer, chan_spec, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int icm45686_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					  size_t *frame_size)
{
	uint8_t pos;
	uint8_t num;

	if (chan_spec.chan_idx != 0U ||
	    icm45686_chan_layout(chan_spec.chan_type, &pos, &num) != 0) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 0U, base_size, frame_size);
}

static int icm45686_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				   uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct icm45686_frame_format fmt;
	struct sensor_raw_frames frames;
	int rc;

	rc = icm45686_get_frames(buffer, chan_spec, &frames, &fmt);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

static bool icm45686_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	struct icm45686_encoded_data *edata = (struct icm45686_encoded_data *)buffer;

	switch (trigger) {
	case SENSOR_TRIG_DATA_READY:
		return edata->header.events & REG_INT1_STATUS0_DRDY(true);
	case SENSOR_TRIG_FIFO_WATERMARK:
		return edata->header.events & REG_INT1_STATUS0_FIFO_THS(true);
	case SENSOR_TRIG_FIFO_FULL:
		return edata->header.events & REG_INT1_STATUS0_FIFO_FULL(true);
	default:
		break;
	}

	return false;
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = icm45686_decoder_get_frame_count,
	.get_size_info = icm45686_decoder_get_size_info,
	.decode = icm45686_decoder_decode,
	.has_trigger = icm45686_decoder_has_trigger,
};

int icm45686_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
