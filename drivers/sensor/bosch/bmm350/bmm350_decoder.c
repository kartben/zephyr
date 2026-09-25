/*
 * Copyright (c) 2025 Croxel, Inc.
 * Copyright (c) 2025 CogniPilot Foundation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdalign.h>

#include <zephyr/drivers/sensor_clock.h>
#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/sys/byteorder.h>

#include "bmm350.h"
#include "bmm350_decoder.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(BMM350_DECODER, CONFIG_SENSOR_LOG_LEVEL);

/* Offset of a data register in struct bmm350_raw_mag_data, after the two dummy bytes */
#define BMM350_RAW_OFFSET(reg) (2U + (reg) - BMM350_REG_MAG_X_XLSB)

/* Output shift for gauss and degC: 8 fractional bits */
#define BMM350_DECODER_SHIFT (31 - 8)

/* Read a signed 24-bit little-endian data register */
static int32_t bmm350_raw_get(const struct bmm350_raw_mag_data *raw_data, uint8_t reg)
{
	return sign_extend(sys_get_le24(&raw_data->buf[BMM350_RAW_OFFSET(reg)]),
			   BMM350_SIGNED_24_BIT);
}

void bmm350_decoder_compensate_raw_data(const struct bmm350_raw_mag_data *raw_data,
					const struct mag_compensate *comp,
					struct bmm350_mag_temp_data *out)
{
	/*
	 * Use 64-bit intermediates: the cross-axis stage scales by
	 * BMM350_MAG_COMP_COEFF_SCALING twice, which overflows int32 for strong
	 * fields (e.g. during self-test or saturation).
	 */
	int64_t out_data[4];
	int64_t t_delta;
	const int64_t scale100 = (int64_t)BMM350_MAG_COMP_COEFF_SCALING * 100;
	int32_t dut_offset_coef[3], dut_sensit_coef[3], dut_tco[3], dut_tcs[3];

	/* Convert mag lsb to uT and temp lsb to centi-degC (0.01 degC) */
	out_data[0] =
		((bmm350_raw_get(raw_data, BMM350_REG_MAG_X_XLSB) * BMM350_LSB_TO_UT_XY_COEFF) /
		 BMM350_LSB_TO_UT_COEFF_DIV);
	out_data[1] =
		((bmm350_raw_get(raw_data, BMM350_REG_MAG_Y_XLSB) * BMM350_LSB_TO_UT_XY_COEFF) /
		 BMM350_LSB_TO_UT_COEFF_DIV);
	out_data[2] =
		((bmm350_raw_get(raw_data, BMM350_REG_MAG_Z_XLSB) * BMM350_LSB_TO_UT_Z_COEFF) /
		 BMM350_LSB_TO_UT_COEFF_DIV);
	out_data[3] = ((int64_t)bmm350_raw_get(raw_data, BMM350_REG_TEMP_XLSB) *
		       BMM350_LSB_TO_UT_TEMP_COEFF * 100) /
		      BMM350_LSB_TO_UT_COEFF_DIV;

	/* Subtract the 25.49 degC offset (expressed in centi-degC). */
	if (out_data[3] > 0) {
		out_data[3] = (out_data[3] - 2549);
	} else if (out_data[3] < 0) {
		out_data[3] = (out_data[3] + 2549);
	}

	/* Apply compensation to temperature reading */
	out_data[3] = (((BMM350_MAG_COMP_COEFF_SCALING + comp->dut_sensit_coef.t_sens) *
			out_data[3]) +
		       ((int64_t)comp->dut_offset_coef.t_offs * 100)) /
		      BMM350_MAG_COMP_COEFF_SCALING;

	/* Store magnetic compensation structure to an array */
	dut_offset_coef[0] = comp->dut_offset_coef.offset_x;
	dut_offset_coef[1] = comp->dut_offset_coef.offset_y;
	dut_offset_coef[2] = comp->dut_offset_coef.offset_z;

	dut_sensit_coef[0] = comp->dut_sensit_coef.sens_x;
	dut_sensit_coef[1] = comp->dut_sensit_coef.sens_y;
	dut_sensit_coef[2] = comp->dut_sensit_coef.sens_z;

	dut_tco[0] = comp->dut_tco.tco_x;
	dut_tco[1] = comp->dut_tco.tco_y;
	dut_tco[2] = comp->dut_tco.tco_z;

	dut_tcs[0] = comp->dut_tcs.tcs_x;
	dut_tcs[1] = comp->dut_tcs.tcs_y;
	dut_tcs[2] = comp->dut_tcs.tcs_z;

	/*
	 * Temperature delta from the calibration reference, in centi-degC. The
	 * TCO/TCS stages below fold in the extra factor of 100 via scale100 so the
	 * temperature correction uses the full 0.01 degC resolution.
	 */
	t_delta = out_data[3] - ((int64_t)comp->dut_t0 * 100);

	/* Compensate raw magnetic data */
	for (size_t indx = 0; indx < 3; indx++) {
		out_data[indx] = (out_data[indx] *
				  (BMM350_MAG_COMP_COEFF_SCALING + dut_sensit_coef[indx])) /
				 BMM350_MAG_COMP_COEFF_SCALING;
		out_data[indx] = (out_data[indx] + dut_offset_coef[indx]);
		out_data[indx] = ((out_data[indx] * scale100) + (dut_tco[indx] * t_delta)) /
				 scale100;
		out_data[indx] = (out_data[indx] * scale100) /
				 (scale100 + (dut_tcs[indx] * t_delta));
	}

	out->mag[0] = (int32_t)((((out_data[0] * BMM350_MAG_COMP_COEFF_SCALING) -
		    (comp->cross_axis.cross_x_y * out_data[1])) *
		   BMM350_MAG_COMP_COEFF_SCALING) /
		  ((BMM350_MAG_COMP_COEFF_SCALING * BMM350_MAG_COMP_COEFF_SCALING) -
		   (comp->cross_axis.cross_y_x * comp->cross_axis.cross_x_y)));

	out->mag[1] = (int32_t)((((out_data[1] * BMM350_MAG_COMP_COEFF_SCALING) -
		    (comp->cross_axis.cross_y_x * out_data[0])) *
		   BMM350_MAG_COMP_COEFF_SCALING) /
		  ((BMM350_MAG_COMP_COEFF_SCALING * BMM350_MAG_COMP_COEFF_SCALING) -
		   (comp->cross_axis.cross_y_x * comp->cross_axis.cross_x_y)));

	out->mag[2] = (int32_t)(out_data[2] +
		  (((out_data[0] *
		     ((comp->cross_axis.cross_y_x * comp->cross_axis.cross_z_y) -
		      (comp->cross_axis.cross_z_x * BMM350_MAG_COMP_COEFF_SCALING))) -
		    (out_data[1] *
		     ((comp->cross_axis.cross_z_y * BMM350_MAG_COMP_COEFF_SCALING) -
		      (comp->cross_axis.cross_x_y * comp->cross_axis.cross_z_x))))) /
		  (((BMM350_MAG_COMP_COEFF_SCALING * BMM350_MAG_COMP_COEFF_SCALING) -
		    comp->cross_axis.cross_y_x * comp->cross_axis.cross_x_y)));

	LOG_DBG("mag data %d %d %d", (int32_t)out_data[0], (int32_t)out_data[1],
		(int32_t)out_data[2]);

	out->temperature = (int32_t)out_data[3];
}

#ifdef CONFIG_SENSOR_ASYNC_API

static uint8_t bmm350_encode_channel(enum sensor_channel chan)
{
	uint8_t encode_bmask = 0;

	switch (chan) {
	case SENSOR_CHAN_MAGN_X:
		encode_bmask |= BIT(0);
		break;
	case SENSOR_CHAN_MAGN_Y:
		encode_bmask |= BIT(1);
		break;
	case SENSOR_CHAN_MAGN_Z:
		encode_bmask |= BIT(2);
		break;
	case SENSOR_CHAN_MAGN_XYZ:
		encode_bmask |= BIT(0);
		encode_bmask |= BIT(1);
		encode_bmask |= BIT(2);
		break;
	case SENSOR_CHAN_DIE_TEMP:
		encode_bmask |= BIT(3);
		break;
	default:
		break;
	}

	return encode_bmask;
}

int bmm350_encode(const struct device *dev,
		  const struct sensor_read_config *read_config,
		  bool is_trigger,
		  uint8_t *buf)
{
	struct bmm350_encoded_data *edata = (struct bmm350_encoded_data *)buf;
	struct bmm350_data *data = dev->data;
	uint64_t cycles;
	int err;

	edata->header.channels = 0;

	if (is_trigger) {
		edata->header.channels |= bmm350_encode_channel(SENSOR_CHAN_MAGN_XYZ);
		edata->header.channels |= bmm350_encode_channel(SENSOR_CHAN_DIE_TEMP);
	} else {
		const struct sensor_chan_spec *const channels = read_config->channels;
		size_t num_channels = read_config->count;

		for (size_t i = 0 ; i < num_channels ; i++) {
			edata->header.channels |= bmm350_encode_channel(channels[i].chan_type);
		}
	}

	err = sensor_clock_get_cycles(&cycles);
	if (err != 0) {
		return err;
	}

	edata->header.events = is_trigger ? BIT(0) : 0;
	edata->header.timestamp = sensor_clock_cycles_to_ns(cycles);

	memcpy(&edata->comp, &data->mag_comp, sizeof(edata->comp));

	return 0;
}

/* The frame pointer is cast to the payload type, which must be accessible at any address */
BUILD_ASSERT(alignof(struct bmm350_raw_mag_data) == 1U);

static int bmm350_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
			       const void *user_data, struct sensor_frame_reading *reading)
{
	const struct bmm350_raw_mag_data *raw = (const struct bmm350_raw_mag_data *)frame;
	struct bmm350_mag_temp_data result;
	const int32_t *values;
	uint8_t num;

	if (reading == NULL) {
		return 1;
	}

	bmm350_decoder_compensate_raw_data(raw, user_data, &result);

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_MAGN_X:
	case SENSOR_CHAN_MAGN_Y:
	case SENSOR_CHAN_MAGN_Z:
		values = &result.mag[chan_spec.chan_type - SENSOR_CHAN_MAGN_X];
		num = 1U;
		break;
	case SENSOR_CHAN_MAGN_XYZ:
		values = result.mag;
		num = 3U;
		break;
	case SENSOR_CHAN_DIE_TEMP:
		values = &result.temperature;
		num = 1U;
		break;
	default:
		return -ENOTSUP;
	}

	/* Magnetic field in uT (0.01 G) to gauss, temperature in 0.01 degC to degC */
	for (uint8_t i = 0U; i < num; i++) {
		reading->values[i] = sensor_raw_to_q31_ratio((uint32_t)values[i], 32U, 1, 100,
							     BMM350_DECODER_SHIFT);
	}

	return 1;
}

/* Describe the payload as a single frame, for a supported channel that was read */
static int bmm350_get_frames(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
			     struct sensor_raw_frames *frames)
{
	const struct bmm350_encoded_data *edata = (const struct bmm350_encoded_data *)buffer;
	uint8_t channel_request = bmm350_encode_channel(chan_spec.chan_type);

	if (chan_spec.chan_idx != 0U || channel_request == 0U) {
		return -ENOTSUP;
	}

	if ((edata->header.channels & channel_request) != channel_request) {
		return -ENODATA;
	}

	*frames = (struct sensor_raw_frames){
		.frames = edata->payload.buf,
		.size = sizeof(edata->payload.buf),
		.frame_size = sizeof(edata->payload.buf),
		.decode_frame = bmm350_decode_frame,
		.user_data = &edata->comp,
		.timestamp_ns = edata->header.timestamp,
		.shift = BMM350_DECODER_SHIFT,
	};

	return 0;
}

static int bmm350_decoder_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
					  uint16_t *frame_count)
{
	struct sensor_raw_frames frames;
	int rc;

	rc = bmm350_get_frames(buffer, chan_spec, &frames);
	if (rc != 0) {
		return rc;
	}

	return sensor_raw_frames_count(&frames, chan_spec, frame_count);
}

static int bmm350_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					size_t *frame_size)
{
	if (bmm350_encode_channel(chan_spec.chan_type) == 0U) {
		return -ENOTSUP;
	}

	return sensor_decode_frames_size_info(chan_spec, 0U, base_size, frame_size);
}

static int bmm350_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				 uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_raw_frames frames;
	int rc;

	rc = bmm350_get_frames(buffer, chan_spec, &frames);
	if (rc != 0) {
		return rc;
	}

	return sensor_decode_frames(&frames, chan_spec, fit, max_count, data_out);
}

static bool bmm350_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	const struct bmm350_encoded_data *edata = (const struct bmm350_encoded_data *)buffer;

	if ((trigger == SENSOR_TRIG_DATA_READY) && (edata->header.events != 0)) {
		return true;
	}

	return false;
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = bmm350_decoder_get_frame_count,
	.get_size_info = bmm350_decoder_get_size_info,
	.decode = bmm350_decoder_decode,
	.has_trigger = bmm350_decoder_has_trigger,
};

int bmm350_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}

#endif /* CONFIG_SENSOR_ASYNC_API */
