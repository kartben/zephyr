/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor_clock.h>
#include <zephyr/logging/log.h>
#include <zephyr/rtio/work.h>

#include "spa06.h"

LOG_MODULE_DECLARE(SPA06, CONFIG_SENSOR_LOG_LEVEL);

static void spa06_submit_one_shot(struct rtio_iodev_sqe *iodev_sqe)
{
	const struct sensor_read_config *read_cfg = iodev_sqe->sqe.iodev->data;
	const struct device *dev = read_cfg->sensor;
	const struct spa06_data *data = dev->data;
	struct spa06_encoded_data *edata;
	uint8_t prs_count = 0U;
	uint8_t tmp_count = 0U;
	uint32_t buf_len;
	uint64_t cycles;
	uint8_t *buf;
	int ret;

	for (size_t i = 0; i < read_cfg->count; i++) {
		switch (read_cfg->channels[i].chan_type) {
		case SENSOR_CHAN_PRESS:
			prs_count = 1U;
			break;
		case SENSOR_CHAN_AMBIENT_TEMP:
			tmp_count = 1U;
			break;
		case SENSOR_CHAN_ALL:
			prs_count = 1U;
			tmp_count = 1U;
			break;
		default:
			break;
		}
	}

	if ((prs_count == 0U) && (tmp_count == 0U)) {
		rtio_iodev_sqe_err(iodev_sqe, -ENOTSUP);
		return;
	}

	ret = spa06_sample_fetch(dev,
				 (prs_count != 0U) ? SENSOR_CHAN_ALL : SENSOR_CHAN_AMBIENT_TEMP);
	if (ret < 0) {
		rtio_iodev_sqe_err(iodev_sqe, ret);
		return;
	}

	ret = sensor_clock_get_cycles(&cycles);
	if (ret < 0) {
		rtio_iodev_sqe_err(iodev_sqe, ret);
		return;
	}

	ret = rtio_sqe_rx_buf(iodev_sqe, sizeof(*edata) + 2U * sizeof(int32_t),
			      sizeof(*edata) + 2U * sizeof(int32_t), &buf, &buf_len);
	if (ret < 0) {
		rtio_iodev_sqe_err(iodev_sqe, ret);
		return;
	}

	edata = (struct spa06_encoded_data *)buf;
	edata->header = (struct spa06_encoded_header){
		.timestamp = sensor_clock_cycles_to_ns(cycles),
		.prs_count = prs_count,
		.tmp_count = tmp_count,
	};

	if (prs_count != 0U) {
		edata->samples[0] = spa06_micro_to_q31(
			spa06_comp_press_micro(&data->coef, data->raw_prs, data->raw_prs_prc,
					       data->raw_tmp, data->raw_tmp_prc),
			SPA06_PRESS_SHIFT);
	}

	if (tmp_count != 0U) {
		edata->samples[prs_count] = spa06_micro_to_q31(
			spa06_comp_temp_micro(&data->coef, data->raw_tmp, data->raw_tmp_prc),
			SPA06_TEMP_SHIFT);
	}

	rtio_iodev_sqe_ok(iodev_sqe, 0);
}

void spa06_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe)
{
	const struct sensor_read_config *read_cfg = iodev_sqe->sqe.iodev->data;
	struct rtio_work_req *req;

	if (read_cfg->is_streaming) {
#ifdef CONFIG_SPA06_STREAM
		spa06_stream_submit(dev, iodev_sqe);
#else
		rtio_iodev_sqe_err(iodev_sqe, -ENOTSUP);
#endif
		return;
	}

	/* Measurements block until the sensor is done, so they run on the RTIO work queue */
	req = rtio_work_req_alloc();
	if (req == NULL) {
		rtio_iodev_sqe_err(iodev_sqe, -ENOMEM);
		return;
	}

	rtio_work_req_submit(req, iodev_sqe, spa06_submit_one_shot);
}
