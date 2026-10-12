/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT goertek_spa06

#include "spa06.h"

static int spa06_decoder_samples(const struct spa06_encoded_data *edata,
				 struct sensor_chan_spec chan_spec, const int32_t **samples,
				 uint8_t *count, uint32_t *period_ns, int8_t *shift)
{
	if (chan_spec.chan_idx != 0U) {
		return -ENOTSUP;
	}

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_PRESS:
		*samples = edata->samples;
		*count = edata->header.prs_count;
		*period_ns = edata->header.prs_period_ns;
		*shift = SPA06_PRESS_SHIFT;
		break;
	case SENSOR_CHAN_AMBIENT_TEMP:
		*samples = &edata->samples[edata->header.prs_count];
		*count = edata->header.tmp_count;
		*period_ns = edata->header.tmp_period_ns;
		*shift = SPA06_TEMP_SHIFT;
		break;
	default:
		return -ENOTSUP;
	}

	return (*count == 0U) ? -ENODATA : 0;
}

static int spa06_decoder_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
					 uint16_t *frame_count)
{
	const struct spa06_encoded_data *edata = (const struct spa06_encoded_data *)buffer;
	const int32_t *samples;
	uint32_t period_ns;
	uint8_t count;
	int8_t shift;
	int ret;

	ret = spa06_decoder_samples(edata, chan_spec, &samples, &count, &period_ns, &shift);
	if (ret == 0) {
		*frame_count = count;
	}

	return ret;
}

static int spa06_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
				       size_t *frame_size)
{
	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_PRESS:
	case SENSOR_CHAN_AMBIENT_TEMP:
		*base_size = sizeof(struct sensor_q31_data);
		*frame_size = sizeof(struct sensor_q31_sample_data);
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int spa06_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				uint32_t *fit, uint16_t max_count, void *data_out)
{
	const struct spa06_encoded_data *edata = (const struct spa06_encoded_data *)buffer;
	struct sensor_q31_data *out = data_out;
	const int32_t *samples;
	uint32_t period_ns;
	uint16_t decoded = 0U;
	uint8_t count;
	int8_t shift;
	int ret;

	if (max_count == 0U) {
		return -EINVAL;
	}

	ret = spa06_decoder_samples(edata, chan_spec, &samples, &count, &period_ns, &shift);
	if (ret < 0) {
		return ret;
	}

	if (*fit >= count) {
		return 0;
	}

	/* The last sample was taken at the header timestamp, earlier ones one period apart */
	out->header.base_timestamp_ns =
		edata->header.timestamp - ((uint64_t)(count - 1U - *fit) * period_ns);
	out->shift = shift;

	while ((*fit < count) && (decoded < max_count)) {
		/* Saturate the 32-bit offset at slow rates */
		out->readings[decoded].timestamp_delta =
			(uint32_t)MIN((uint64_t)decoded * period_ns, UINT32_MAX);
		out->readings[decoded].value = samples[*fit];
		decoded++;
		(*fit)++;
	}

	out->header.reading_count = decoded;

	return decoded;
}

static bool spa06_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	const struct spa06_encoded_data *edata = (const struct spa06_encoded_data *)buffer;

	switch (trigger) {
	case SENSOR_TRIG_DATA_READY:
		return (edata->header.events & SPA06_EVENT_DRDY) != 0U;
	case SENSOR_TRIG_FIFO_FULL:
		return (edata->header.events & SPA06_EVENT_FIFO_FULL) != 0U;
	default:
		return false;
	}
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = spa06_decoder_get_frame_count,
	.get_size_info = spa06_decoder_get_size_info,
	.decode = spa06_decoder_decode,
	.has_trigger = spa06_decoder_has_trigger,
};

int spa06_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);

	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
