/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor_clock.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "spa06.h"

LOG_MODULE_DECLARE(SPA06, CONFIG_SENSOR_LOG_LEVEL);

/* Enable the interrupt and FIFO configuration for the given events, or disable it */
static int spa06_stream_enable(const struct device *dev, uint8_t events)
{
	struct spa06_data *data = dev->data;
	bool fifo_was_enabled = (data->int_cfg & SPA06_CFG_REG_FIFO_EN) != 0U;
	uint8_t int_sts;
	int ret;

	switch (events) {
	case SPA06_EVENT_DRDY:
		data->int_cfg = SPA06_CFG_REG_INT_PRS;
		break;
	case SPA06_EVENT_FIFO_FULL:
		data->int_cfg = SPA06_CFG_REG_FIFO_EN | SPA06_CFG_REG_INT_FIFO;
		break;
	default:
		data->int_cfg = 0U;
		break;
	}

	data->stream_events = events;

	ret = spa06_write_cfg_reg(dev);
	if (ret < 0) {
		return ret;
	}

	/* Drop results from before the FIFO was enabled, or left over after it */
	if ((events == SPA06_EVENT_FIFO_FULL) || fifo_was_enabled) {
		ret = spa06_reg_write(dev, SPA06_REG_RESET, SPA06_RESET_FIFO_FLUSH);
		if (ret < 0) {
			return ret;
		}
	}

	/* Clear a pending status, so that the next event asserts the line */
	return spa06_reg_read(dev, SPA06_REG_INT_STS, &int_sts, 1);
}

void spa06_stream_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe)
{
	const struct sensor_read_config *read_cfg = iodev_sqe->sqe.iodev->data;
	const struct spa06_config *cfg = dev->config;
	struct spa06_data *data = dev->data;
	uint8_t events = 0U;
	int ret;

	for (size_t i = 0; i < read_cfg->count; i++) {
		const struct sensor_stream_trigger *trig = &read_cfg->triggers[i];

		if (trig->opt != SENSOR_STREAM_DATA_INCLUDE) {
			rtio_iodev_sqe_err(iodev_sqe, -ENOTSUP);
			return;
		}

		switch (trig->trigger) {
		case SENSOR_TRIG_DATA_READY:
			events |= SPA06_EVENT_DRDY;
			break;
		case SENSOR_TRIG_FIFO_FULL:
			events |= SPA06_EVENT_FIFO_FULL;
			break;
		default:
			rtio_iodev_sqe_err(iodev_sqe, -ENOTSUP);
			return;
		}
	}

	if ((events != SPA06_EVENT_DRDY) && (events != SPA06_EVENT_FIFO_FULL)) {
		LOG_ERR("Stream either data-ready or FIFO-full events");
		rtio_iodev_sqe_err(iodev_sqe, -ENOTSUP);
		return;
	}

	if (cfg->int_gpio.port == NULL) {
		rtio_iodev_sqe_err(iodev_sqe, -ENOTSUP);
		return;
	}

	if (!spa06_is_active(dev)) {
		rtio_iodev_sqe_err(iodev_sqe, -EBUSY);
		return;
	}

	if (data->prs_odr == SPA06_ODR_NONE) {
		LOG_ERR("Streaming needs continuous measurements");
		rtio_iodev_sqe_err(iodev_sqe, -EINVAL);
		return;
	}

	if (data->drdy_handler != NULL) {
		rtio_iodev_sqe_err(iodev_sqe, -EBUSY);
		return;
	}

	/* Publish the request first: an interrupt may follow as soon as it is enabled */
	atomic_ptr_set(&data->stream_sqe, iodev_sqe);

	if (events != data->stream_events) {
		ret = spa06_stream_enable(dev, events);
		if ((ret < 0) && atomic_ptr_cas(&data->stream_sqe, iodev_sqe, NULL)) {
			rtio_iodev_sqe_err(iodev_sqe, ret);
		}
	}
}

static int spa06_stream_read_drdy(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe,
				  uint64_t timestamp)
{
	struct spa06_data *data = dev->data;
	const uint32_t len = sizeof(struct spa06_encoded_data) + (2U * sizeof(int32_t));
	struct spa06_encoded_data *edata;
	uint8_t regs[6];
	uint32_t buf_len;
	uint8_t *buf;
	int ret;

	ret = spa06_reg_read(dev, SPA06_REG_PSR_B2, regs, sizeof(regs));
	if (ret < 0) {
		return ret;
	}

	data->raw_prs = sign_extend(sys_get_be24(&regs[0]), 23);
	data->raw_tmp = sign_extend(sys_get_be24(&regs[3]), 23);
	data->raw_prs_prc = data->prs_prc;
	data->raw_tmp_prc = data->tmp_prc;

	ret = rtio_sqe_rx_buf(iodev_sqe, len, len, &buf, &buf_len);
	if (ret < 0) {
		return ret;
	}

	edata = (struct spa06_encoded_data *)buf;
	edata->header = (struct spa06_encoded_header){
		.timestamp = timestamp,
		.events = SPA06_EVENT_DRDY,
		.prs_count = 1U,
		.tmp_count = 1U,
	};
	edata->samples[0] =
		spa06_micro_to_q31(spa06_comp_press_micro(&data->coef, data->raw_prs, data->prs_prc,
							  data->raw_tmp, data->tmp_prc),
				   SPA06_PRESS_SHIFT);
	edata->samples[1] = spa06_micro_to_q31(
		spa06_comp_temp_micro(&data->coef, data->raw_tmp, data->tmp_prc), SPA06_TEMP_SHIFT);

	return 0;
}

static int spa06_stream_read_fifo(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe,
				  uint64_t timestamp)
{
	struct spa06_data *data = dev->data;
	uint32_t entries[SPA06_FIFO_SIZE];
	struct spa06_encoded_data *edata;
	uint8_t prs_count = 0U;
	uint8_t tmp_count = 0U;
	int32_t raw_tmp = data->raw_tmp;
	bool have_tmp = false;
	uint8_t count = 0U;
	int32_t *prs_out;
	int32_t *tmp_out;
	uint32_t buf_len;
	uint32_t len;
	uint8_t *buf;
	int ret;

	/* Each read of the pressure result registers returns the next FIFO entry */
	while (count < SPA06_FIFO_SIZE) {
		uint8_t regs[3];

		ret = spa06_reg_read(dev, SPA06_REG_PSR_B2, regs, sizeof(regs));
		if (ret < 0) {
			return ret;
		}

		entries[count] = sys_get_be24(regs);
		if (entries[count] == SPA06_FIFO_EMPTY) {
			break;
		}

		if ((entries[count] & SPA06_FIFO_PRS) != 0U) {
			prs_count++;
		} else {
			tmp_count++;
			/* Entries before the first temperature are compensated with it */
			if (!have_tmp) {
				raw_tmp = sign_extend(entries[count], 23);
				have_tmp = true;
			}
		}

		count++;
	}

	len = sizeof(struct spa06_encoded_data) + ((uint32_t)count * sizeof(int32_t));
	ret = rtio_sqe_rx_buf(iodev_sqe, len, len, &buf, &buf_len);
	if (ret < 0) {
		return ret;
	}

	edata = (struct spa06_encoded_data *)buf;
	edata->header = (struct spa06_encoded_header){
		.timestamp = timestamp,
		.prs_period_ns = spa06_odr_period_ns(data->prs_odr),
		.tmp_period_ns = spa06_odr_period_ns(data->tmp_odr),
		.events = SPA06_EVENT_FIFO_FULL,
		.prs_count = prs_count,
		.tmp_count = tmp_count,
	};

	prs_out = edata->samples;
	tmp_out = &edata->samples[prs_count];

	for (uint8_t i = 0U; i < count; i++) {
		int32_t raw = sign_extend(entries[i], 23);

		if ((entries[i] & SPA06_FIFO_PRS) != 0U) {
			*prs_out++ = spa06_micro_to_q31(
				spa06_comp_press_micro(&data->coef, raw, data->prs_prc, raw_tmp,
						       data->tmp_prc),
				SPA06_PRESS_SHIFT);
			data->raw_prs = raw;
		} else {
			raw_tmp = raw;
			*tmp_out++ = spa06_micro_to_q31(
				spa06_comp_temp_micro(&data->coef, raw, data->tmp_prc),
				SPA06_TEMP_SHIFT);
		}
	}

	data->raw_tmp = raw_tmp;
	data->raw_prs_prc = data->prs_prc;
	data->raw_tmp_prc = data->tmp_prc;

	return 0;
}

bool spa06_stream_handle_interrupt(const struct device *dev)
{
	struct spa06_data *data = dev->data;
	struct rtio_iodev_sqe *iodev_sqe;
	uint64_t timestamp = 0U;
	uint64_t cycles;
	uint8_t int_sts;
	int ret;

	if (data->stream_events == 0U) {
		return false;
	}

	if (sensor_clock_get_cycles(&cycles) == 0) {
		timestamp = sensor_clock_cycles_to_ns(cycles);
	}

	/* Reading INT_STS clears it and releases the interrupt line */
	ret = spa06_reg_read(dev, SPA06_REG_INT_STS, &int_sts, 1);

	iodev_sqe = atomic_ptr_clear(&data->stream_sqe);
	if ((iodev_sqe == NULL) || (FIELD_GET(RTIO_SQE_CANCELED, iodev_sqe->sqe.flags) != 0U)) {
		/* Nobody waits for data anymore */
		(void)spa06_stream_enable(dev, 0U);
		if (iodev_sqe != NULL) {
			rtio_iodev_sqe_err(iodev_sqe, -ECANCELED);
		}
		return true;
	}

	if (ret == 0) {
		if ((data->stream_events == SPA06_EVENT_FIFO_FULL) &&
		    ((int_sts & SPA06_INT_STS_FIFO_FULL) != 0U)) {
			ret = spa06_stream_read_fifo(dev, iodev_sqe, timestamp);
			if (ret == 0) {
				ret = spa06_reg_write(dev, SPA06_REG_RESET, SPA06_RESET_FIFO_FLUSH);
			}
		} else if ((data->stream_events == SPA06_EVENT_DRDY) &&
			   ((int_sts & SPA06_INT_STS_PRS) != 0U)) {
			ret = spa06_stream_read_drdy(dev, iodev_sqe, timestamp);
		} else {
			/* Not an event of this stream, keep waiting */
			atomic_ptr_set(&data->stream_sqe, iodev_sqe);
			return true;
		}
	}

	if (ret < 0) {
		rtio_iodev_sqe_err(iodev_sqe, ret);
	} else {
		rtio_iodev_sqe_ok(iodev_sqe, 0);
	}

	return true;
}
