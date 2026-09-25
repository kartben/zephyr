/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT bosch_bmi270

#include <errno.h>
#include <stddef.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "bmi270.h"
#include "bmi270_decoder.h"

/*
 * BMI270 FIFO header byte layout
 *   Bits [7:6] = fh_mode  (10 = regular, 01 = control)
 *   Bits [5:2] = fh_parm  (sensor presence for regular; opcode for control)
 *   Bits [1:0] = fh_ext   (INT tag bits)
 *
 * fh_parm for regular frames:
 *   bit 0 = ACC, bit 1 = GYR, bit 2 = AUX, bit 3 = reserved
 */
#define BMI270_FIFO_HDR_MODE(h)             (((h) >> 6) & 0x03U)
#define BMI270_FIFO_HDR_PARM(h)             (((h) >> 2) & 0x0FU)
#define BMI270_FIFO_MODE_REGULAR            0x02U
#define BMI270_FIFO_MODE_CONTROL            0x01U
#define BMI270_FIFO_PARM_ACC                BIT(0)
#define BMI270_FIFO_PARM_GYR                BIT(1)
#define BMI270_FIFO_CTRL_PARM_SKIP_FRAME    0x00U
#define BMI270_FIFO_CTRL_PARM_SENSORTIME    0x01U
#define BMI270_FIFO_CTRL_PARM_CONFIG_CHANGE 0x02U

#define BMI270_FIFO_SENSOR_BYTES           6U
#define BMI270_FIFO_PAYLOAD_ACC_GYR        12U
#define BMI270_FIFO_REGULAR_LEN_EMPTY      2
#define BMI270_FIFO_CTRL_LEN_SKIP_FRAME    2
#define BMI270_FIFO_CTRL_LEN_SENSORTIME    4
#define BMI270_FIFO_CTRL_LEN_CONFIG_CHANGE 5

#define BMI270_ACC_SHIFT_BASE 5
#define BMI270_GYR_SHIFT      6

/* Samples are 16-bit two's complement, full scale is +/-32768 LSB */
#define BMI270_SAMPLE_BITS    16U
#define BMI270_FULL_SCALE_LSB 32768LL

/*
 * The accelerometer shift grows by one when the range doubles, so the scale factor is the same
 * for all ranges: range_g * g / 32768 per LSB with shift 5 + log2(range_g / 2).
 */
#define BMI270_ACC_SCALE                                                                           \
	SENSOR_Q31_SCALE((int64_t)SENSOR_G * 2, BMI270_FULL_SCALE_LSB * 1000000LL,                 \
			 BMI270_ACC_SHIFT_BASE)

/* range_dps * pi / 180 / 32768 rad/s per LSB */
#define BMI270_GYR_SCALE(range_dps)                                                                \
	SENSOR_Q31_SCALE((int64_t)(range_dps) * SENSOR_PI,                                         \
			 180LL * BMI270_FULL_SCALE_LSB * 1000000LL, BMI270_GYR_SHIFT)

/* Indexed by bmi270_decoder_header.gyr_range_idx */
static const int32_t gyr_scale[] = {
	BMI270_GYR_SCALE(2000), BMI270_GYR_SCALE(1000), BMI270_GYR_SCALE(500),
	BMI270_GYR_SCALE(250),  BMI270_GYR_SCALE(125),
};

/*
 * Sample period indexed by the ACC_CONF.acc_odr and GYR_CONF.gyr_odr register value, which
 * both use the same encoding.
 */
static const uint64_t odr_period_ns[] = {
	/* 25/32 Hz and 25/16 Hz are not a whole number of millihertz */
	[BMI270_ACC_ODR_25D32_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(25000) * 32U,
	[BMI270_ACC_ODR_25D16_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(25000) * 16U,
	[BMI270_ACC_ODR_25D8_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(3125),
	[BMI270_ACC_ODR_25D4_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(6250),
	[BMI270_ACC_ODR_25D2_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(12500),
	[BMI270_ACC_ODR_25_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(25000),
	[BMI270_ACC_ODR_50_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(50000),
	[BMI270_ACC_ODR_100_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(100000),
	[BMI270_ACC_ODR_200_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(200000),
	[BMI270_ACC_ODR_400_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(400000),
	[BMI270_ACC_ODR_800_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(800000),
	[BMI270_ACC_ODR_1600_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(1600000),
	[BMI270_GYR_ODR_3200_HZ] = SENSOR_ODR_MHZ_TO_PERIOD_NS(3200000),
};

struct bmi270_frame_format {
	int32_t scale;
	bool is_headerless;
};

static bool bmi270_chan_type_is_supported(struct sensor_chan_spec chan_spec)
{
	return chan_spec.chan_type == SENSOR_CHAN_ACCEL_XYZ ||
	       chan_spec.chan_type == SENSOR_CHAN_GYRO_XYZ;
}

static int bmi270_fifo_control_frame_len(uint8_t parm)
{
	switch (parm) {
	case BMI270_FIFO_CTRL_PARM_SENSORTIME:
		return BMI270_FIFO_CTRL_LEN_SENSORTIME;
	case BMI270_FIFO_CTRL_PARM_CONFIG_CHANGE:
		return BMI270_FIFO_CTRL_LEN_CONFIG_CHANGE;
	case BMI270_FIFO_CTRL_PARM_SKIP_FRAME:
	default:
		return BMI270_FIFO_CTRL_LEN_SKIP_FRAME;
	}
}

static uint8_t bmi270_fifo_payload_len(uint8_t parm)
{
	return (((parm & BMI270_FIFO_PARM_ACC) != 0U) ? BMI270_FIFO_SENSOR_BYTES : 0U) +
	       (((parm & BMI270_FIFO_PARM_GYR) != 0U) ? BMI270_FIFO_SENSOR_BYTES : 0U);
}

/* Header mode: length of the frame starting with the header byte at frame[0] */
static int bmi270_fifo_frame_len(const uint8_t *frame, size_t remaining, const void *user_data)
{
	uint8_t parm = BMI270_FIFO_HDR_PARM(frame[0]);

	ARG_UNUSED(remaining);
	ARG_UNUSED(user_data);

	switch (BMI270_FIFO_HDR_MODE(frame[0])) {
	case BMI270_FIFO_MODE_REGULAR:
		if (parm == 0U) {
			/* Over-read marker 0x80 0x00 */
			return BMI270_FIFO_REGULAR_LEN_EMPTY;
		}
		return 1 + bmi270_fifo_payload_len(parm);
	case BMI270_FIFO_MODE_CONTROL:
		return bmi270_fifo_control_frame_len(parm);
	default:
		return 1;
	}
}

/*
 * Frame payload order: GYR (6 bytes, if present) then ACC (6 bytes, if present). Headerless
 * frames always carry both.
 */
static int bmi270_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
			       const void *user_data, struct sensor_frame_reading *reading)
{
	const struct bmi270_frame_format *fmt = user_data;
	bool is_accel = chan_spec.chan_type == SENSOR_CHAN_ACCEL_XYZ;
	const uint8_t *payload;

	if (fmt->is_headerless) {
		payload = is_accel ? &frame[BMI270_FIFO_SENSOR_BYTES] : frame;
	} else {
		uint8_t parm = BMI270_FIFO_HDR_PARM(frame[0]);
		bool has_acc = (parm & BMI270_FIFO_PARM_ACC) != 0U;
		bool has_gyr = (parm & BMI270_FIFO_PARM_GYR) != 0U;

		if (BMI270_FIFO_HDR_MODE(frame[0]) != BMI270_FIFO_MODE_REGULAR ||
		    (is_accel ? !has_acc : !has_gyr)) {
			return 0;
		}

		payload = &frame[1];
		if (is_accel && has_gyr) {
			payload += BMI270_FIFO_SENSOR_BYTES;
		}
	}

	if (reading == NULL) {
		return 1;
	}

	for (uint8_t i = 0U; i < 3U; i++) {
		reading->values[i] = sensor_raw_to_q31(sys_get_le16(&payload[i * 2U]),
						       BMI270_SAMPLE_BITS, fmt->scale);
	}

	return 1;
}

static void bmi270_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
			      struct sensor_raw_frames *frames, struct bmi270_frame_format *fmt)
{
	const struct bmi270_fifo_encoded_data *edata =
		(const struct bmi270_fifo_encoded_data *)buffer;
	uint8_t odr;

	*frames = (struct sensor_raw_frames){
		.frames = edata->fifo_data,
		.size = edata->fifo_byte_count,
		.decode_frame = bmi270_decode_frame,
		.user_data = fmt,
		.timestamp_ns = edata->header.timestamp,
	};

	fmt->is_headerless = edata->header.is_headerless != 0U;
	if (fmt->is_headerless) {
		frames->frame_size = BMI270_FIFO_PAYLOAD_ACC_GYR;
	} else {
		frames->frame_len = bmi270_fifo_frame_len;
	}

	if (chan_spec.chan_type == SENSOR_CHAN_ACCEL_XYZ) {
		frames->shift = BMI270_ACC_SHIFT_BASE + edata->header.acc_range;
		fmt->scale = BMI270_ACC_SCALE;
		odr = edata->header.acc_odr;
	} else {
		frames->shift = BMI270_GYR_SHIFT;
		/* Unknown ranges decode as +/-2000 dps */
		fmt->scale = (edata->header.gyr_range_idx < ARRAY_SIZE(gyr_scale))
				     ? gyr_scale[edata->header.gyr_range_idx]
				     : gyr_scale[0];
		odr = edata->header.gyr_odr;
	}

	/* A disabled or unknown ODR gives all readings the timestamp of the buffer */
	frames->period_ns = (odr < ARRAY_SIZE(odr_period_ns)) ? odr_period_ns[odr] : 0U;
}

static int bmi270_decoder_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
					  uint16_t *frame_count)
{
	const struct bmi270_fifo_encoded_data *edata =
		(const struct bmi270_fifo_encoded_data *)buffer;
	struct sensor_raw_frames frames;
	struct bmi270_frame_format fmt;

	if (chan_spec.chan_idx != 0U) {
		return -ENOTSUP;
	}

	if (edata->header.is_fifo == 0U) {
		return -ENODATA;
	}

	if (!bmi270_chan_type_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	bmi270_get_frames(buffer, chan_spec, &frames, &fmt);

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int bmi270_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					size_t *frame_size)
{
	if (chan_spec.chan_idx != 0U || !bmi270_chan_type_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 0U, base_size, frame_size);
}

static int bmi270_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				 uint32_t *fit, uint16_t max_count, void *data_out)
{
	const struct bmi270_fifo_encoded_data *edata =
		(const struct bmi270_fifo_encoded_data *)buffer;
	struct sensor_raw_frames frames;
	struct bmi270_frame_format fmt;

	if (chan_spec.chan_idx != 0U) {
		return -ENOTSUP;
	}

	if (edata->header.is_fifo == 0U) {
		return -EINVAL;
	}

	if (!bmi270_chan_type_is_supported(chan_spec)) {
		return -ENOTSUP;
	}

	bmi270_get_frames(buffer, chan_spec, &frames, &fmt);

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

static bool bmi270_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	const struct bmi270_fifo_encoded_data *edata =
		(const struct bmi270_fifo_encoded_data *)buffer;

	if (edata->header.is_fifo == 0U) {
		return false;
	}

	switch (trigger) {
	case SENSOR_TRIG_FIFO_WATERMARK:
		return (edata->header.int_status & BMI270_INT_STATUS_1_FWM_INT) != 0U;
	case SENSOR_TRIG_FIFO_FULL:
		return (edata->header.int_status & BMI270_INT_STATUS_1_FFULL_INT) != 0U;
	default:
		return false;
	}
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = bmi270_decoder_get_frame_count,
	.get_size_info = bmi270_decoder_get_size_info,
	.decode = bmi270_decoder_decode,
	.has_trigger = bmi270_decoder_has_trigger,
};

int bmi270_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
