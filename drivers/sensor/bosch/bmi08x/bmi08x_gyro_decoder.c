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

#define DT_DRV_COMPAT bosch_bmi08x_gyro
#include "bmi08x.h"
#include "bmi08x_bus.h"
#include "bmi08x_gyro_stream.h"
#include "bmi08x_gyro_decoder.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(BMI08X_GYRO_DECODER, CONFIG_SENSOR_LOG_LEVEL);

/* Period in ns indexed by BMI08X_GYRO_BW_* register value (0x00..0x07) */
static const uint64_t gyro_period_ns[] = {
	[BMI08X_GYRO_BW_532_ODR_2000_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(2000000),
	[BMI08X_GYRO_BW_230_ODR_2000_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(2000000),
	[BMI08X_GYRO_BW_116_ODR_1000_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(1000000),
	[BMI08X_GYRO_BW_47_ODR_400_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(400000),
	[BMI08X_GYRO_BW_23_ODR_200_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(200000),
	[BMI08X_GYRO_BW_12_ODR_100_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(100000),
	[BMI08X_GYRO_BW_64_ODR_200_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(200000),
	[BMI08X_GYRO_BW_32_ODR_100_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(100000),
};

/*
 * At the 2000 dps range, one LSB is 2000 / 32768 dps; the ratio in rad/s is reduced by 16000 to
 * fit in 32 bits. Each range step halves the LSB and 2^shift, so the q31 value does not depend on
 * the range. Bits needed for the integer part in rad/s: 6 at 2000 dps (34.91 rad/s), 5 at
 * 1000 dps, 4 at 500 dps, 3 at 250 dps and 2 at 125 dps.
 */
BUILD_ASSERT((2000LL * SENSOR_PI) % 16000LL == 0);
#define BMI08X_GYRO_LSB_NUM    ((int32_t)(2000LL * SENSOR_PI / 16000LL))
#define BMI08X_GYRO_LSB_DEN    ((int32_t)(32768LL * 180LL * 1000000LL / 16000LL))
#define BMI08X_GYRO_BASE_SHIFT 6

void bmi08x_gyro_encode_header(const struct device *dev, struct bmi08x_gyro_encoded_data *edata,
			       bool is_streaming)
{
	struct bmi08x_gyro_data *data = dev->data;
	const struct bmi08x_gyro_config *config = dev->config;
	uint64_t cycles;

	if (sensor_clock_get_cycles(&cycles) == 0) {
		edata->header.timestamp = sensor_clock_cycles_to_ns(cycles);
	} else {
		edata->header.timestamp = 0;
	}
	edata->header.has_gyro = true;
	edata->header.range = data->range;
	edata->header.is_streaming = is_streaming;
	edata->header.sample_count = is_streaming ? data->stream.fifo_wm : 1;
	edata->header.gyro_odr = config->gyro_hz;
}

static int bmi08x_gyro_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				    const void *user_data, struct sensor_frame_reading *reading)
{
	ARG_UNUSED(user_data);

	if (chan_spec.chan_type != SENSOR_CHAN_GYRO_XYZ) {
		return -ENOTSUP;
	}

	if (reading == NULL) {
		return 1;
	}

	for (uint8_t i = 0U; i < 3U; i++) {
		reading->values[i] = sensor_raw_to_q31_ratio(
			sys_get_le16(&frame[i * 2U]), 16U, BMI08X_GYRO_LSB_NUM, BMI08X_GYRO_LSB_DEN,
			BMI08X_GYRO_BASE_SHIFT);
	}

	return 1;
}

static int bmi08x_gyro_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				  struct sensor_raw_frames *frames)
{
	const struct bmi08x_gyro_encoded_data *edata =
		(const struct bmi08x_gyro_encoded_data *)buffer;
	size_t frame_count = 1U;

	if (chan_spec.chan_type != SENSOR_CHAN_GYRO_XYZ || chan_spec.chan_idx != 0U) {
		return -ENOTSUP;
	}

	if (!edata->header.has_gyro) {
		return -ENODATA;
	}

	*frames = (struct sensor_raw_frames){
		.frames = (const uint8_t *)edata->fifo,
		.frame_size = sizeof(struct bmi08x_gyro_frame),
		.decode_frame = bmi08x_gyro_decode_frame,
		.timestamp_ns = edata->header.timestamp,
		.shift = BMI08X_GYRO_BASE_SHIFT - edata->header.range,
	};

	if (edata->header.is_streaming) {
		/* FIFO_STATUS bits 6:0 hold the number of frames in the FIFO, bit 7 the overrun */
		frame_count =
			MIN(edata->header.sample_count, edata->header.fifo_status & BIT_MASK(7));
		if (edata->header.gyro_odr < ARRAY_SIZE(gyro_period_ns)) {
			frames->period_ns = gyro_period_ns[edata->header.gyro_odr];
		}
	}

	frames->size = frame_count * sizeof(struct bmi08x_gyro_frame);

	return 0;
}

static int bmi08x_decoder_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
					  uint16_t *frame_count)
{
	struct sensor_raw_frames frames;
	int ret;

	ret = bmi08x_gyro_get_frames(buffer, chan_spec, &frames);
	if (ret != 0) {
		return ret;
	}

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int bmi08x_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					size_t *frame_size)
{
	if (chan_spec.chan_type != SENSOR_CHAN_GYRO_XYZ || chan_spec.chan_idx != 0U) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 0U, base_size, frame_size);
}

static int bmi08x_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				 uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_raw_frames frames;
	int ret;

	ret = bmi08x_gyro_get_frames(buffer, chan_spec, &frames);
	if (ret != 0) {
		return ret;
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

static bool bmi08x_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	const struct bmi08x_gyro_encoded_data *edata =
		(const struct bmi08x_gyro_encoded_data *)buffer;

	return trigger == SENSOR_TRIG_FIFO_WATERMARK &&
	       edata->header.has_gyro &&
	       edata->header.is_streaming &&
	       edata->header.int_status & BIT(4) &&
	       (edata->header.fifo_status & BIT_MASK(7)) > 0;
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = bmi08x_decoder_get_frame_count,
	.get_size_info = bmi08x_decoder_get_size_info,
	.decode = bmi08x_decoder_decode,
	.has_trigger = bmi08x_decoder_has_trigger,
};

int bmi08x_gyro_decoder_get(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();
	return 0;
}
