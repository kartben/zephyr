/* ST Microelectronics IIS3DWB accelerometer sensor
 *
 * Copyright (c) 2025 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Datasheet:
 * https://www.st.com/resource/en/datasheet/iis3dwb.pdf
 */

#define DT_DRV_COMPAT st_iis3dwb

#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/sys/byteorder.h>

#include "iis3dwb.h"

/* The tag of a FIFO word is in bits 7:3 of its first byte */
#define IIS3DWB_FIFO_TAG_SHIFT 3U

/* Accelerometer shift for a given range value */
static const int8_t accel_shift[] = {
	[IIS3DWB_DT_FS_2G] = 5,
	[IIS3DWB_DT_FS_4G] = 6,
	[IIS3DWB_DT_FS_8G] = 7,
	[IIS3DWB_DT_FS_16G] = 8,
};

/*
 * One LSB is GAIN_UNIT ug, that is GAIN_UNIT * SENSOR_G / 10^12 m/s^2, at +/-2 g. The
 * sensitivity doubles at each range step while the shift grows by one, so the scale factor is
 * the same for all ranges.
 */
#define IIS3DWB_ACCEL_SCALE SENSOR_Q31_SCALE(GAIN_UNIT * SENSOR_G, 1000000000000LL, 5)

/* Temperature: 256 LSB/degC, 0 LSB at 25 degC */
#define IIS3DWB_TEMP_SHIFT  9
#define IIS3DWB_TEMP_SCALE  SENSOR_Q31_SCALE(1, 256, IIS3DWB_TEMP_SHIFT)
#define IIS3DWB_TEMP_OFFSET SENSOR_Q31_SCALE(25, 1, IIS3DWB_TEMP_SHIFT)

struct iis3dwb_frame_format {
	/* Frames are FIFO words (a tag byte and 6 data bytes) instead of output registers */
	bool tagged;
};

static const struct iis3dwb_frame_format fifo_format = {.tagged = true};
static const struct iis3dwb_frame_format sample_format = {.tagged = false};

static bool iis3dwb_chan_is_supported(struct sensor_chan_spec chan_spec)
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
		return IS_ENABLED(CONFIG_IIS3DWB_ENABLE_TEMP);
	default:
		return false;
	}
}

static int iis3dwb_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				const void *user_data, struct sensor_frame_reading *reading)
{
	const struct iis3dwb_frame_format *fmt = user_data;
	const uint8_t *data = frame;
	uint8_t tag;
	uint8_t first = 0U;
	uint8_t num = 1U;

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_ACCEL_XYZ:
		tag = IIS3DWB_XL_TAG;
		num = 3U;
		break;
	case SENSOR_CHAN_ACCEL_X:
		tag = IIS3DWB_XL_TAG;
		break;
	case SENSOR_CHAN_ACCEL_Y:
		tag = IIS3DWB_XL_TAG;
		first = 1U;
		break;
	case SENSOR_CHAN_ACCEL_Z:
		tag = IIS3DWB_XL_TAG;
		first = 2U;
		break;
	case SENSOR_CHAN_DIE_TEMP:
		tag = IIS3DWB_TEMPERATURE_TAG;
		break;
	default:
		return -ENOTSUP;
	}

	if (fmt->tagged) {
		if ((frame[0] >> IIS3DWB_FIFO_TAG_SHIFT) != tag) {
			/* Word of another sensor, or a timestamp */
			return 0;
		}
		data = &frame[1];
	}

	if (reading == NULL) {
		return 1;
	}

	if (tag == IIS3DWB_TEMPERATURE_TAG) {
		reading->values[0] =
			sensor_raw_to_q31(sys_get_le16(data), 16U, IIS3DWB_TEMP_SCALE) +
			IIS3DWB_TEMP_OFFSET;
		return 1;
	}

	for (uint8_t i = 0U; i < num; i++) {
		reading->values[i] = sensor_raw_to_q31(sys_get_le16(&data[(first + i) * 2U]), 16U,
						       IIS3DWB_ACCEL_SCALE);
	}

	return 1;
}

static uint64_t iis3dwb_fifo_period_ns(const struct iis3dwb_fifo_data *hdr,
				       struct sensor_chan_spec chan_spec)
{
	if (chan_spec.chan_type == SENSOR_CHAN_DIE_TEMP) {
		return (hdr->temp_batch_odr == IIS3DWB_DT_TEMP_BATCHED_AT_104Hz)
			       ? SENSOR_ODR_MHZ_TO_PERIOD_NS(104000)
			       : 0U;
	}

	return (hdr->accel_batch_odr == IIS3DWB_DT_XL_BATCHED_AT_26k7Hz)
		       ? SENSOR_ODR_MHZ_TO_PERIOD_NS(26700000)
		       : 0U;
}

static void iis3dwb_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
			       struct sensor_raw_frames *frames)
{
	const struct iis3dwb_fifo_data *fifo = (const struct iis3dwb_fifo_data *)buffer;
	const struct iis3dwb_rtio_data *sample = (const struct iis3dwb_rtio_data *)buffer;
	const struct iis3dwb_decoder_header *header = &fifo->header;

	*frames = (struct sensor_raw_frames){
		.decode_frame = iis3dwb_decode_frame,
		.timestamp_ns = header->timestamp,
		.shift = (chan_spec.chan_type == SENSOR_CHAN_DIE_TEMP) ? IIS3DWB_TEMP_SHIFT
								       : accel_shift[header->range],
	};

	if (IS_ENABLED(CONFIG_IIS3DWB_STREAM) && header->is_fifo == 1U) {
		/* The header timestamp is the one of the newest word of each sensor */
		frames->frames = buffer + sizeof(*fifo);
		frames->size = IIS3DWB_FIFO_SIZE(fifo->fifo_count);
		frames->frame_size = IIS3DWB_FIFO_ITEM_LEN;
		frames->user_data = &fifo_format;
		frames->period_ns = iis3dwb_fifo_period_ns(fifo, chan_spec);
	} else if (chan_spec.chan_type == SENSOR_CHAN_DIE_TEMP) {
		frames->frames = (const uint8_t *)&sample->temp;
		frames->frame_size = sizeof(sample->temp);
		frames->size = (sample->has_temp == 1U) ? frames->frame_size : 0U;
		frames->user_data = &sample_format;
	} else {
		frames->frames = (const uint8_t *)sample->accel;
		frames->frame_size = sizeof(sample->accel);
		frames->size = (sample->has_accel == 1U) ? frames->frame_size : 0U;
		frames->user_data = &sample_format;
	}
}

static int iis3dwb_decoder_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
					   uint16_t *frame_count)
{
	struct sensor_raw_frames frames;

	if (!iis3dwb_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	iis3dwb_get_frames(buffer, chan_spec, &frames);

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int iis3dwb_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				  uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_raw_frames frames;

	if (!iis3dwb_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	iis3dwb_get_frames(buffer, chan_spec, &frames);

	if (frames.user_data == &sample_format && frames.size == 0U) {
		/* The requested sensor was not read */
		return -ENODATA;
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

static int iis3dwb_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					 size_t *frame_size)
{
	if (!iis3dwb_chan_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 0U, base_size, frame_size);
}

static bool iis3dwb_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	const struct iis3dwb_decoder_header *header = (const struct iis3dwb_decoder_header *)buffer;

	if (!IS_ENABLED(CONFIG_IIS3DWB_STREAM)) {
		return false;
	}

	/* int_status holds STATUS_REG for single samples, FIFO_STATUS2 for FIFO data */
	switch (trigger) {
	case SENSOR_TRIG_DATA_READY:
		return header->is_fifo == 0U && (header->int_status & 0x01U) != 0U;
	case SENSOR_TRIG_FIFO_WATERMARK:
		return header->is_fifo == 1U && (header->int_status & 0x80U) != 0U;
	case SENSOR_TRIG_FIFO_FULL:
		return header->is_fifo == 1U && (header->int_status & 0x20U) != 0U;
	default:
		return false;
	}
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = iis3dwb_decoder_get_frame_count,
	.get_size_info = iis3dwb_decoder_get_size_info,
	.decode = iis3dwb_decoder_decode,
	.has_trigger = iis3dwb_decoder_has_trigger,
};

int iis3dwb_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
