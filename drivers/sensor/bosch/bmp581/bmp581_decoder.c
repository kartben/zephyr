/*
 * Copyright (c) 2025 Croxel, Inc.
 * Copyright (c) 2025 CogniPilot Foundation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor_clock.h>
#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/dt-bindings/sensor/bmp581.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include "bmp581.h"
#include "bmp581_decoder.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(BMP581_DECODER, CONFIG_SENSOR_LOG_LEVEL);

/* Temperature and pressure are decoded with the same shift */
#define BMP581_SHIFT 15

/* Temperature: 24-bit two's complement, 1/65536 degC per LSB */
#define BMP581_TEMP_OFFSET 0U
#define BMP581_TEMP_BITS   24U
#define BMP581_TEMP_SCALE  SENSOR_Q31_SCALE(1, 65536, BMP581_SHIFT)

/* Pressure: 24-bit unsigned, 1/64 Pa per LSB, decoded in kPa */
#define BMP581_PRESS_OFFSET      3U
#define BMP581_PRESS_BITS        25U
#define BMP581_PRESS_LSB_PER_KPA 64000

static const uint64_t odr_period_ns[] = {
	[BMP581_DT_ODR_240_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(240000),
	[BMP581_DT_ODR_218_5_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(218500),
	[BMP581_DT_ODR_199_1_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(199100),
	[BMP581_DT_ODR_179_2_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(179200),
	[BMP581_DT_ODR_160_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(160000),
	[BMP581_DT_ODR_149_3_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(149300),
	[BMP581_DT_ODR_140_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(140000),
	[BMP581_DT_ODR_129_8_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(129800),
	[BMP581_DT_ODR_120_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(120000),
	[BMP581_DT_ODR_110_1_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(110100),
	[BMP581_DT_ODR_100_2_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(100200),
	[BMP581_DT_ODR_89_6_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(89600),
	[BMP581_DT_ODR_80_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(80000),
	[BMP581_DT_ODR_70_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(70000),
	[BMP581_DT_ODR_60_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(60000),
	[BMP581_DT_ODR_50_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(50000),
	[BMP581_DT_ODR_45_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(45000),
	[BMP581_DT_ODR_40_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(40000),
	[BMP581_DT_ODR_35_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(35000),
	[BMP581_DT_ODR_30_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(30000),
	[BMP581_DT_ODR_25_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(25000),
	[BMP581_DT_ODR_20_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(20000),
	[BMP581_DT_ODR_15_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(15000),
	[BMP581_DT_ODR_10_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(10000),
	[BMP581_DT_ODR_5_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(5000),
	[BMP581_DT_ODR_4_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(4000),
	[BMP581_DT_ODR_3_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(3000),
	[BMP581_DT_ODR_2_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(2000),
	[BMP581_DT_ODR_1_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(1000),
	[BMP581_DT_ODR_0_5_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(500),
	[BMP581_DT_ODR_0_250_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(250),
	[BMP581_DT_ODR_0_125_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(125),
};

static uint8_t bmp581_encode_channel(enum sensor_channel chan)
{
	uint8_t encode_bmask = 0;

	switch (chan) {
	case SENSOR_CHAN_AMBIENT_TEMP:
		encode_bmask |= BIT(0);
		break;
	case SENSOR_CHAN_PRESS:
		encode_bmask |= BIT(1);
		break;
	case SENSOR_CHAN_ALL:
		encode_bmask |= BIT(0) | BIT(1);
		break;
	default:
		break;
	}

	return encode_bmask;
}

int bmp581_encode(const struct device *dev,
		  const struct sensor_read_config *read_config,
		  uint8_t trigger_status,
		  uint8_t *buf)
{
	struct bmp581_encoded_data *edata = (struct bmp581_encoded_data *)buf;
	struct bmp581_data *data = dev->data;

	edata->header.channels = 0;
	edata->header.press_en = data->osr_odr_press_config.press_en;
	edata->header.odr = data->osr_odr_press_config.odr;

	if (trigger_status) {
		edata->header.channels |= bmp581_encode_channel(SENSOR_CHAN_ALL);
		edata->header.fifo_count = data->stream.fifo_thres;
		edata->header.timestamp = data->stream.timestamp;
	} else {
		const struct sensor_chan_spec *const channels = read_config->channels;
		size_t num_channels = read_config->count;

		for (size_t i = 0; i < num_channels; i++) {
			edata->header.channels |= bmp581_encode_channel(channels[i].chan_type);
		}

		uint64_t cycles;
		int err = sensor_clock_get_cycles(&cycles);

		if (err != 0) {
			return err;
		}
		edata->header.timestamp = sensor_clock_cycles_to_ns(cycles);
	}

	edata->header.events = trigger_status;

	return 0;
}

static bool bmp581_chan_is_supported(struct sensor_chan_spec chan_spec)
{
	return chan_spec.chan_idx == 0U && (chan_spec.chan_type == SENSOR_CHAN_AMBIENT_TEMP ||
					    chan_spec.chan_type == SENSOR_CHAN_PRESS);
}

static bool bmp581_chan_has_data(const struct bmp581_encoded_header *header,
				 struct sensor_chan_spec chan_spec)
{
	uint8_t channel_request = bmp581_encode_channel(chan_spec.chan_type);

	if ((header->channels & channel_request) != channel_request) {
		return false;
	}

	return chan_spec.chan_type != SENSOR_CHAN_PRESS || header->press_en != 0U;
}

static int bmp581_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
			       const void *user_data, struct sensor_frame_reading *reading)
{
	ARG_UNUSED(user_data);

	if (chan_spec.chan_type != SENSOR_CHAN_AMBIENT_TEMP &&
	    chan_spec.chan_type != SENSOR_CHAN_PRESS) {
		return -ENOTSUP;
	}

	if (reading == NULL) {
		return 1;
	}

	if (chan_spec.chan_type == SENSOR_CHAN_AMBIENT_TEMP) {
		reading->values[0] = sensor_raw_to_q31(sys_get_le24(&frame[BMP581_TEMP_OFFSET]),
						       BMP581_TEMP_BITS, BMP581_TEMP_SCALE);
	} else {
		reading->values[0] = sensor_raw_to_q31_ratio(
			sys_get_le24(&frame[BMP581_PRESS_OFFSET]), BMP581_PRESS_BITS, 1,
			BMP581_PRESS_LSB_PER_KPA, BMP581_SHIFT);
	}

	return 1;
}

static int bmp581_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
			     struct sensor_raw_frames *frames)
{
	const struct bmp581_encoded_data *edata = (const struct bmp581_encoded_data *)buffer;
	size_t frame_count = 1U;

	if (!bmp581_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	/* Channel not requested in a one-shot read, or pressure disabled */
	if (!bmp581_chan_has_data(&edata->header, chan_spec)) {
		return -ENODATA;
	}

	*frames = (struct sensor_raw_frames){
		.frames = edata->payload,
		.frame_size = sizeof(struct bmp581_frame),
		.decode_frame = bmp581_decode_frame,
		.timestamp_ns = edata->header.timestamp,
		.shift = BMP581_SHIFT,
	};

	if ((edata->header.events & BMP581_EVENT_FIFO_WM) != 0U) {
		if (edata->header.odr >= ARRAY_SIZE(odr_period_ns)) {
			return -EINVAL;
		}

		frame_count = edata->header.fifo_count;
		frames->period_ns = odr_period_ns[edata->header.odr];
	}

	frames->size = frame_count * sizeof(struct bmp581_frame);

	return 0;
}

static int bmp581_decoder_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
					  uint16_t *frame_count)
{
	struct sensor_raw_frames frames;
	int rc;

	rc = bmp581_get_frames(buffer, chan_spec, &frames);
	if (rc != 0) {
		return rc;
	}

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int bmp581_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					size_t *frame_size)
{
	if (!bmp581_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 0U, base_size, frame_size);
}

static int bmp581_decoder_decode(const uint8_t *buffer,
				struct sensor_chan_spec chan_spec,
				uint32_t *fit,
				uint16_t max_count,
				void *data_out)
{
	struct sensor_raw_frames frames;
	int rc;

	rc = bmp581_get_frames(buffer, chan_spec, &frames);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

static bool bmp581_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	const struct bmp581_encoded_data *edata = (const struct bmp581_encoded_data *)buffer;

	if ((trigger == SENSOR_TRIG_DATA_READY &&
	     edata->header.events & BMP581_EVENT_DRDY) ||
	    (trigger == SENSOR_TRIG_FIFO_WATERMARK &&
	     edata->header.events & BMP581_EVENT_FIFO_WM)) {
		return true;
	}

	return false;
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = bmp581_decoder_get_frame_count,
	.get_size_info = bmp581_decoder_get_size_info,
	.decode = bmp581_decoder_decode,
	.has_trigger = bmp581_decoder_has_trigger,
};

int bmp581_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();
	return 0;
}
