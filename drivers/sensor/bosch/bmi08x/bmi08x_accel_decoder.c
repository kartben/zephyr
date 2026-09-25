/*
 * Copyright (c) 2025 Croxel, Inc.
 * Copyright (c) 2025 CogniPilot Foundation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor_clock.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/check.h>

#define DT_DRV_COMPAT bosch_bmi08x_accel
#include "bmi08x.h"
#include "bmi08x_bus.h"
#include "bmi08x_accel_decoder.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(BMI08X_ACCEL_DECODER, CONFIG_SENSOR_LOG_LEVEL);

enum bmi08x_accel_fifo_header {
	BMI08X_ACCEL_FIFO_FRAME_ACCEL = 0x84,
	BMI08X_ACCEL_FIFO_FRAME_SKIP = 0x40,
	BMI08X_ACCEL_FIFO_FRAME_TIME = 0x44,
	BMI08X_ACCEL_FIFO_FRAME_CONFIG = 0x48,
	BMI08X_ACCEL_FIFO_FRAME_DROP = 0x50,
	BMI08X_ACCEL_FIFO_FRAME_EMPTY = 0x80,
};

/* The two low bits of a frame header are interrupt tags */
#define BMI08X_ACCEL_FIFO_HEADER_MASK 0xFCU

static const struct frame_len {
	enum bmi08x_accel_fifo_header header;
	uint8_t len;
} fifo_frame_len[] = {
	{.header = BMI08X_ACCEL_FIFO_FRAME_ACCEL, .len = 7},
	{.header = BMI08X_ACCEL_FIFO_FRAME_SKIP, .len = 2},
	{.header = BMI08X_ACCEL_FIFO_FRAME_TIME, .len = 4},
	{.header = BMI08X_ACCEL_FIFO_FRAME_CONFIG, .len = 2},
	{.header = BMI08X_ACCEL_FIFO_FRAME_DROP, .len = 2},
	{.header = BMI08X_ACCEL_FIFO_FRAME_EMPTY, .len = 2},
};

/* Period in ns indexed by BMI08X_ACCEL_ODR_* register value (0x05 = 12.5 Hz .. 0x0C = 1600 Hz) */
static const uint64_t accel_period_ns[] = {
	[BMI08X_ACCEL_ODR_12_5_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(12500),
	[BMI08X_ACCEL_ODR_25_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(25000),
	[BMI08X_ACCEL_ODR_50_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(50000),
	[BMI08X_ACCEL_ODR_100_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(100000),
	[BMI08X_ACCEL_ODR_200_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(200000),
	[BMI08X_ACCEL_ODR_400_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(400000),
	[BMI08X_ACCEL_ODR_800_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(800000),
	[BMI08X_ACCEL_ODR_1600_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(1600000),
};

/*
 * At the lowest range, one LSB is fsr_g / 32768 g, with fsr_g = 2 for the BMI085 and 3 for the
 * BMI088; the ratio is reduced by 50 to fit in 32 bits. Each range step doubles both the LSB
 * and 2^shift, so the q31 value does not depend on the range. Bits needed for the integer
 * part in m/s^2: 5 at 2 - 3 g, 6 at 4 - 6 g, 7 at 8 - 12 g and 8 at 16 - 24 g.
 */
BUILD_ASSERT(SENSOR_G % 50 == 0);
#define BMI08X_ACCEL_LSB_NUM(fsr_g) ((int32_t)((fsr_g) * (SENSOR_G / 50)))
#define BMI08X_ACCEL_LSB_DEN        ((int32_t)(32768 * (1000000 / 50)))
#define BMI08X_ACCEL_BASE_SHIFT     5

struct bmi08x_accel_frame_format {
	int32_t lsb_num;
	bool is_fifo;
};

void bmi08x_accel_encode_header(const struct device *dev, struct bmi08x_accel_encoded_data *edata,
			       bool is_streaming, uint16_t buf_len)
{
	struct bmi08x_accel_data *data = dev->data;
	const struct bmi08x_accel_config *config = dev->config;
	uint64_t cycles;

	if (sensor_clock_get_cycles(&cycles) == 0) {
		edata->header.timestamp = sensor_clock_cycles_to_ns(cycles);
	} else {
		edata->header.timestamp = 0;
	}
	edata->header.has_accel = true;
	edata->header.range = data->range;
	edata->header.chip_id = data->accel_chip_id;
	edata->header.is_streaming = is_streaming;
	edata->header.sample_count = is_streaming ? data->stream.fifo_wm : 1;
	edata->header.buf_len = buf_len;
	edata->header.accel_odr = config->accel_hz;
}

static int bmi08x_accel_fifo_frame_len(const uint8_t *frame, size_t remaining,
				       const void *user_data)
{
	uint8_t header = frame[0] & BMI08X_ACCEL_FIFO_HEADER_MASK;

	ARG_UNUSED(remaining);
	ARG_UNUSED(user_data);

	for (size_t i = 0; i < ARRAY_SIZE(fifo_frame_len); i++) {
		if (header == fifo_frame_len[i].header) {
			return fifo_frame_len[i].len;
		}
	}

	/* The rest of the data cannot be parsed: keep the frames read so far */
	LOG_WRN_RATELIMIT("Invalid frame header: 0x%02X", header);

	return 0;
}

static int bmi08x_accel_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				     const void *user_data, struct sensor_frame_reading *reading)
{
	const struct bmi08x_accel_frame_format *fmt = user_data;
	const uint8_t *payload = frame;

	if (chan_spec.chan_type != SENSOR_CHAN_ACCEL_XYZ) {
		return -ENOTSUP;
	}

	if (fmt->is_fifo) {
		if ((frame[0] & BMI08X_ACCEL_FIFO_HEADER_MASK) != BMI08X_ACCEL_FIFO_FRAME_ACCEL) {
			return 0;
		}
		payload = &frame[1];
	}

	if (reading == NULL) {
		return 1;
	}

	for (uint8_t i = 0U; i < 3U; i++) {
		reading->values[i] =
			sensor_raw_to_q31_ratio(sys_get_le16(&payload[i * 2U]), 16U, fmt->lsb_num,
						BMI08X_ACCEL_LSB_DEN, BMI08X_ACCEL_BASE_SHIFT);
	}

	return 1;
}

static int bmi08x_accel_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				   struct sensor_raw_frames *frames,
				   struct bmi08x_accel_frame_format *fmt)
{
	const struct bmi08x_accel_encoded_data *edata =
		(const struct bmi08x_accel_encoded_data *)buffer;

	if (chan_spec.chan_type != SENSOR_CHAN_ACCEL_XYZ || chan_spec.chan_idx != 0U) {
		return -ENOTSUP;
	}

	if (!edata->header.has_accel) {
		return -ENODATA;
	}

	fmt->lsb_num = (edata->header.chip_id == BMI085_ACCEL_CHIP_ID) ? BMI08X_ACCEL_LSB_NUM(2)
								       : BMI08X_ACCEL_LSB_NUM(3);
	fmt->is_fifo = edata->header.is_streaming;

	*frames = (struct sensor_raw_frames){
		.decode_frame = bmi08x_accel_decode_frame,
		.user_data = fmt,
		.timestamp_ns = edata->header.timestamp,
		.shift = BMI08X_ACCEL_BASE_SHIFT + edata->header.range,
	};

	if (edata->header.is_streaming) {
		frames->frames = edata->fifo;
		frames->size = edata->header.buf_len;
		frames->frame_len = bmi08x_accel_fifo_frame_len;
		if (edata->header.accel_odr < ARRAY_SIZE(accel_period_ns)) {
			frames->period_ns = accel_period_ns[edata->header.accel_odr];
		}
	} else {
		frames->frames = (const uint8_t *)edata->payload;
		frames->size = sizeof(edata->payload);
		frames->frame_size = sizeof(edata->payload);
	}

	return 0;
}

static int bmi08x_decoder_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
					  uint16_t *frame_count)
{
	struct sensor_raw_frames frames;
	struct bmi08x_accel_frame_format fmt;
	int ret;

	ret = bmi08x_accel_get_frames(buffer, chan_spec, &frames, &fmt);
	if (ret != 0) {
		return ret;
	}

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int bmi08x_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					size_t *frame_size)
{
	if (chan_spec.chan_type != SENSOR_CHAN_ACCEL_XYZ || chan_spec.chan_idx != 0U) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 0U, base_size, frame_size);
}

static int bmi08x_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				 uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_raw_frames frames;
	struct bmi08x_accel_frame_format fmt;
	int ret;

	ret = bmi08x_accel_get_frames(buffer, chan_spec, &frames, &fmt);
	if (ret != 0) {
		return ret;
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

static bool bmi08x_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	const struct bmi08x_accel_encoded_data *edata =
		(const struct bmi08x_accel_encoded_data *)buffer;

	return edata->header.is_streaming && edata->header.fifo_len > 0 &&
	       trigger == SENSOR_TRIG_FIFO_WATERMARK;
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = bmi08x_decoder_get_frame_count,
	.get_size_info = bmi08x_decoder_get_size_info,
	.decode = bmi08x_decoder_decode,
	.has_trigger = bmi08x_decoder_has_trigger,
};

int bmi08x_accel_decoder_get(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();
	return 0;
}
