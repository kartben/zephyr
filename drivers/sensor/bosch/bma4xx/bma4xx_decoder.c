/*
 * Copyright (c) 2024 Cienet
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT bosch_bma4xx

#include "bma4xx_decoder.h"
#include "bma4xx_defs.h"
#include "bma4xx.h"
#include <errno.h>
#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(bma4xx, CONFIG_SENSOR_LOG_LEVEL);

/* Accelerometer samples are 12-bit, left-justified in 16-bit little endian words */
#define BMA4XX_ACCEL_BITS 12U

/*
 * At +/-2 g the sensitivity is 1024 LSB/g and the shift is 5. Each range step halves the
 * sensitivity and adds one to the shift, so the scale factor is the same for all ranges.
 */
#define BMA4XX_ACCEL_QSCALE SENSOR_Q31_SCALE(SENSOR_G, 1024 * 1000000LL, 5)

/* Die temperature: 1 degree C per LSB, 0 is 23 degrees C */
#define BMA4XX_TEMP_QSCALE SENSOR_Q31_SCALE(1, 1, BMA4XX_TEMP_SHIFT)
#define BMA4XX_TEMP_OFFSET 23

/* Offset of the temperature in a single sample, after the accelerometer data */
#define BMA4XX_SAMPLE_TEMP_OFFSET BMA4XX_FIFO_A_LENGTH

static bool bma4xx_is_fifo(const uint8_t *buffer)
{
	const struct bma4xx_decoder_header *header = (const struct bma4xx_decoder_header *)buffer;

	return IS_ENABLED(CONFIG_BMA4XX_STREAM) && header->is_fifo == 1U;
}

static bool bma4xx_chan_is_supported(struct sensor_chan_spec chan_spec, bool is_fifo)
{
	if (chan_spec.chan_idx != 0U) {
		return false;
	}

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z:
	case SENSOR_CHAN_ACCEL_XYZ:
		return true;
	case SENSOR_CHAN_DIE_TEMP:
		return IS_ENABLED(CONFIG_BMA4XX_TEMPERATURE) && !is_fifo;
	default:
		return false;
	}
}

/* Get the length of a frame of a FIFO read in header mode */
static int bma4xx_fifo_frame_len(const uint8_t *frame, size_t remaining, const void *user_data)
{
	const uint8_t header = frame[0];
	int len = BMA4XX_FIFO_HEADER_LENGTH;

	ARG_UNUSED(remaining);
	ARG_UNUSED(user_data);

	if (FIELD_GET(BMA4XX_BIT_FIFO_HEADER_REGULAR, header) != 0U) {
		if (FIELD_GET(BMA4XX_BIT_FIFO_HEADER_ACCEL, header) != 0U) {
			len += BMA4XX_FIFO_A_LENGTH;
		}
		if (FIELD_GET(BMA4XX_BIT_FIFO_HEADER_AUX, header) != 0U) {
			len += BMA4XX_FIFO_M_LENGTH;
		}
		/* A regular frame without data marks a read past the end of the FIFO */
		return (len > BMA4XX_FIFO_HEADER_LENGTH) ? len : 0;
	}

	if (FIELD_GET(BMA4XX_BIT_FIFO_HEADER_CONTROL, header) != 0U) {
		if (FIELD_GET(BMA4XX_BIT_FIFO_HEADER_SENSORTIME, header) != 0U) {
			return len + BMA4XX_FIFO_ST_LENGTH;
		}
		if (FIELD_GET(BMA4XX_BIT_FIFO_HEAD_OVER_READ_MSB, header) != 0U) {
			return 0;
		}
		return len + BMA4XX_FIFO_CF_LENGTH;
	}

	/* Invalid header, end of the data */
	return 0;
}

static int bma4xx_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
			       const void *user_data, struct sensor_frame_reading *reading)
{
	const bool *is_fifo = user_data;
	const uint8_t *accel = frame;
	uint8_t first;
	uint8_t num;

	if (*is_fifo) {
		/* Only regular frames carrying accelerometer data hold a reading */
		if (FIELD_GET(BMA4XX_BIT_FIFO_HEADER_REGULAR, frame[0]) == 0U ||
		    FIELD_GET(BMA4XX_BIT_FIFO_HEADER_ACCEL, frame[0]) == 0U) {
			return 0;
		}

		accel += BMA4XX_FIFO_HEADER_LENGTH;
		/* Auxiliary data comes first */
		if (FIELD_GET(BMA4XX_BIT_FIFO_HEADER_AUX, frame[0]) != 0U) {
			accel += BMA4XX_FIFO_M_LENGTH;
		}
	}

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_ACCEL_XYZ:
		first = 0U;
		num = 3U;
		break;
	case SENSOR_CHAN_ACCEL_X:
		first = 0U;
		num = 1U;
		break;
	case SENSOR_CHAN_ACCEL_Y:
		first = 1U;
		num = 1U;
		break;
	case SENSOR_CHAN_ACCEL_Z:
		first = 2U;
		num = 1U;
		break;
	case SENSOR_CHAN_DIE_TEMP:
		if (reading != NULL) {
			int32_t temp = (int8_t)frame[BMA4XX_SAMPLE_TEMP_OFFSET];

			reading->values[0] = (temp + BMA4XX_TEMP_OFFSET) * BMA4XX_TEMP_QSCALE;
		}
		return 1;
	default:
		return -ENOTSUP;
	}

	if (reading == NULL) {
		return 1;
	}

	for (uint8_t i = 0U; i < num; i++) {
		reading->values[i] = sensor_raw_to_q31(sys_get_le16(&accel[(first + i) * 2U]) >> 4,
						       BMA4XX_ACCEL_BITS, BMA4XX_ACCEL_QSCALE);
	}

	return 1;
}

/* The output data rate is 100 Hz * 2^(odr - BMA4XX_ODR_100) */
static uint64_t bma4xx_accel_period_ns(uint8_t odr)
{
	const uint64_t period_100hz = SENSOR_ODR_MHZ_TO_PERIOD_NS(100000);

	if (odr == BMA4XX_ODR_RESERVED) {
		return 0U;
	}

	if (odr >= BMA4XX_ODR_100) {
		return period_100hz >> (odr - BMA4XX_ODR_100);
	}

	return period_100hz << (BMA4XX_ODR_100 - odr);
}

static void bma4xx_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
			      const bool *is_fifo, struct sensor_raw_frames *frames)
{
	const struct bma4xx_decoder_header *header = (const struct bma4xx_decoder_header *)buffer;

	*frames = (struct sensor_raw_frames){
		.decode_frame = bma4xx_decode_frame,
		.user_data = is_fifo,
		.timestamp_ns = header->timestamp,
		/* accel_fs is 0 to 3 for +/-2 g to +/-16 g */
		.shift = (chan_spec.chan_type == SENSOR_CHAN_DIE_TEMP) ? BMA4XX_TEMP_SHIFT
								       : 5 + header->accel_fs,
	};

	if (*is_fifo) {
		const struct bma4xx_fifo_data *fdata = (const struct bma4xx_fifo_data *)buffer;

		frames->frames = buffer + sizeof(*fdata);
		frames->size = fdata->fifo_count;
		frames->frame_len = bma4xx_fifo_frame_len;
		frames->period_ns = bma4xx_accel_period_ns(fdata->accel_odr);
	} else {
		const struct bma4xx_encoded_data *edata =
			(const struct bma4xx_encoded_data *)buffer;

		frames->frames = edata->accel_xyz_raw_data;
		frames->size =
			sizeof(*edata) - offsetof(struct bma4xx_encoded_data, accel_xyz_raw_data);
		frames->frame_size = frames->size;
	}
}

/*
 * RTIO decoder
 */

static int bma4xx_decoder_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec ch,
					  uint16_t *frame_count)
{
	const bool is_fifo = bma4xx_is_fifo(buffer);
	struct sensor_raw_frames frames;

	if (!bma4xx_chan_is_supported(ch, is_fifo)) {
		return -ENOTSUP;
	}

	bma4xx_get_frames(buffer, ch, &is_fifo, &frames);

	return sensor_raw_frames_count(&frames, ch, frame_count);
}

static int bma4xx_decoder_get_size_info(struct sensor_chan_spec ch, size_t *base_size,
					size_t *frame_size)
{
	if (!bma4xx_chan_is_supported(ch, false)) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(ch, 0U, base_size, frame_size);
}

static int bma4xx_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec ch, uint32_t *fit,
				 uint16_t max_count, void *data_out)
{
	const bool is_fifo = bma4xx_is_fifo(buffer);
	struct sensor_raw_frames frames;

	if (!bma4xx_chan_is_supported(ch, is_fifo)) {
		return -ENOTSUP;
	}

	bma4xx_get_frames(buffer, ch, &is_fifo, &frames);

	return sensor_decode_frames(&frames, ch, fit, max_count, data_out);
}

static bool bma4xx_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	const struct bma4xx_fifo_data *fdata = (const struct bma4xx_fifo_data *)buffer;

	if (!bma4xx_is_fifo(buffer)) {
		return false;
	}

	switch (trigger) {
	case SENSOR_TRIG_FIFO_WATERMARK:
		return FIELD_GET(BMA4XX_BIT_INT_STAT_1_FWM_INT, fdata->int_status) != 0U;
	case SENSOR_TRIG_FIFO_FULL:
		return FIELD_GET(BMA4XX_BIT_INT_STAT_1_FFULL_INT, fdata->int_status) != 0U;
	default:
		return false;
	}
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = bma4xx_decoder_get_frame_count,
	.get_size_info = bma4xx_decoder_get_size_info,
	.decode = bma4xx_decoder_decode,
	.has_trigger = bma4xx_decoder_has_trigger,
};

int bma4xx_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
