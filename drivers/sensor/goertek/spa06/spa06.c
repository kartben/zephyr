/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Goertek SPA06-003 barometric pressure and temperature sensor.
 *
 * Register map, scale factors and compensation formulas follow the
 * SPA06-003 datasheet, version 2.0.
 */

#define DT_DRV_COMPAT goertek_spa06

#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/math_extras.h>

#include "spa06.h"

LOG_MODULE_REGISTER(SPA06, CONFIG_SENSOR_LOG_LEVEL);

/* The datasheet does not specify how long coefficient loading takes after reset */
#define SPA06_STARTUP_TIMEOUT K_MSEC(100)
/* Allowed delay past the nominal measurement time before giving up */
#define SPA06_MEAS_TIMEOUT_MS 10U
#define SPA06_POLL_INTERVAL   K_MSEC(1)

/* Fixed-point scale of the intermediate pressure compensation terms */
#define SPA06_FRAC_SCALE 4096LL

/* Compensation scale factors kP and kT (datasheet table 4), indexed by oversampling setting */
static const int32_t spa06_scale_factor[] = {
	524288, 1572864, 3670016, 7864320, 253952, 516096, 1040384, 2088960,
};

/* Measurement time in microseconds (datasheet table 8), indexed by oversampling setting */
static const uint32_t spa06_meas_time_us[] = {
	3600, 5200, 8400, 14800, 27600, 53200, 104400, 206800,
};

/* Measurement rates in micro-hertz, indexed by rate code */
static const uint32_t spa06_odr_uhz[] = {
	1000000, 2000000, 4000000, 8000000,  16000000, 32000000, 64000000,  128000000,
	1562500, 3125000, 6250000, 12500000, 25000000, 50000000, 100000000, 200000000,
};

int spa06_reg_read(const struct device *dev, uint8_t reg, uint8_t *buf, size_t len)
{
	const struct spa06_config *cfg = dev->config;

	return cfg->bus_io->read(&cfg->bus, reg, buf, len);
}

int spa06_reg_write(const struct device *dev, uint8_t reg, uint8_t val)
{
	const struct spa06_config *cfg = dev->config;

	return cfg->bus_io->write(&cfg->bus, reg, val);
}

uint32_t spa06_odr_period_ns(uint8_t odr)
{
	if (odr >= ARRAY_SIZE(spa06_odr_uhz)) {
		return 0U;
	}

	return (uint32_t)(((uint64_t)NSEC_PER_SEC * USEC_PER_SEC) / spa06_odr_uhz[odr]);
}

int spa06_write_cfg_reg(const struct device *dev)
{
	struct spa06_data *data = dev->data;
	uint8_t val = data->int_cfg;

	if (data->prs_prc > SPA06_PRC_8X) {
		val |= SPA06_CFG_REG_P_SHIFT;
	}

	if (data->tmp_prc > SPA06_PRC_8X) {
		val |= SPA06_CFG_REG_T_SHIFT;
	}

#ifdef CONFIG_SPA06_TRIGGER
	const struct spa06_config *cfg = dev->config;

	if ((cfg->int_gpio.port != NULL) && ((cfg->int_gpio.dt_flags & GPIO_ACTIVE_LOW) == 0U)) {
		val |= SPA06_CFG_REG_INT_HL;
	}
#endif

	return spa06_reg_write(dev, SPA06_REG_CFG_REG, val);
}

static int spa06_wait_ready(const struct device *dev, uint8_t mask, k_timeout_t timeout)
{
	k_timepoint_t end = sys_timepoint_calc(timeout);
	uint8_t meas_cfg;
	int ret;

	/* Bus errors are retried: the sensor may not respond while it restarts */
	do {
		ret = spa06_reg_read(dev, SPA06_REG_MEAS_CFG, &meas_cfg, 1);
		if ((ret == 0) && ((meas_cfg & mask) == mask)) {
			return 0;
		}

		k_sleep(SPA06_POLL_INTERVAL);
	} while (!sys_timepoint_expired(end));

	return (ret < 0) ? ret : -ETIMEDOUT;
}

/* Check that the measurements fit in the sensor's time budget of one second per second */
static bool spa06_timing_ok(uint8_t prs_odr, uint8_t prs_prc, uint8_t tmp_odr, uint8_t tmp_prc)
{
	uint64_t busy;

	if (prs_odr == SPA06_ODR_NONE) {
		return true;
	}

	/* Micro-hertz times microseconds: one second per second is 10^12 */
	busy = ((uint64_t)spa06_odr_uhz[prs_odr] * spa06_meas_time_us[prs_prc]) +
	       ((uint64_t)spa06_odr_uhz[tmp_odr] * spa06_meas_time_us[tmp_prc]);

	return busy < ((uint64_t)USEC_PER_SEC * USEC_PER_SEC);
}

/* Apply the current settings and start continuous measurements when a rate is set */
static int spa06_configure(const struct device *dev)
{
	struct spa06_data *data = dev->data;
	uint8_t prs_rate = (data->prs_odr == SPA06_ODR_NONE) ? 0U : data->prs_odr;
	int ret;

	ret = spa06_reg_write(dev, SPA06_REG_MEAS_CFG, SPA06_MEAS_CTRL_IDLE);
	if (ret < 0) {
		return ret;
	}

	ret = spa06_reg_write(dev, SPA06_REG_PRS_CFG,
			      FIELD_PREP(SPA06_CFG_RATE_MASK, prs_rate) |
				      FIELD_PREP(SPA06_CFG_PRC_MASK, data->prs_prc));
	if (ret < 0) {
		return ret;
	}

	ret = spa06_reg_write(dev, SPA06_REG_TMP_CFG,
			      FIELD_PREP(SPA06_CFG_RATE_MASK, data->tmp_odr) |
				      FIELD_PREP(SPA06_CFG_PRC_MASK, data->tmp_prc));
	if (ret < 0) {
		return ret;
	}

	ret = spa06_write_cfg_reg(dev);
	if (ret < 0) {
		return ret;
	}

	data->have_results = false;

	if (data->prs_odr == SPA06_ODR_NONE) {
		return 0;
	}

	return spa06_reg_write(dev, SPA06_REG_MEAS_CFG, SPA06_MEAS_CTRL_CONTINUOUS);
}

static int spa06_measure(const struct device *dev, uint8_t meas_ctrl, uint8_t rdy_mask, uint8_t prc)
{
	int ret;

	ret = spa06_reg_write(dev, SPA06_REG_MEAS_CFG, meas_ctrl);
	if (ret < 0) {
		return ret;
	}

	k_usleep(spa06_meas_time_us[prc]);

	return spa06_wait_ready(dev, rdy_mask, K_MSEC(SPA06_MEAS_TIMEOUT_MS));
}

/* Longest wait for the first pressure and temperature results in continuous mode */
static k_timeout_t spa06_first_results_timeout(const struct spa06_data *data)
{
	uint32_t ms = (spa06_odr_period_ns(data->prs_odr) / NSEC_PER_MSEC) +
		      (spa06_odr_period_ns(data->tmp_odr) / NSEC_PER_MSEC) +
		      (spa06_meas_time_us[data->prs_prc] / USEC_PER_MSEC) +
		      (spa06_meas_time_us[data->tmp_prc] / USEC_PER_MSEC) + SPA06_MEAS_TIMEOUT_MS;

	return K_MSEC(ms);
}

int spa06_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	struct spa06_data *data = dev->data;
	uint8_t buf[6];
	int ret = 0;

	if ((chan != SENSOR_CHAN_ALL) && (chan != SENSOR_CHAN_PRESS) &&
	    (chan != SENSOR_CHAN_AMBIENT_TEMP)) {
		return -ENOTSUP;
	}

	if (!spa06_is_active(dev)) {
		return -EBUSY;
	}

	/* With the FIFO enabled, the result registers return FIFO entries */
	if ((data->int_cfg & SPA06_CFG_REG_FIFO_EN) != 0U) {
		return -EBUSY;
	}

	if (data->prs_odr == SPA06_ODR_NONE) {
		/* Pressure compensation needs the temperature, so it is always measured */
		ret = spa06_measure(dev, SPA06_MEAS_CTRL_TMP, SPA06_MEAS_CFG_TMP_RDY,
				    data->tmp_prc);
		if ((ret == 0) && (chan != SENSOR_CHAN_AMBIENT_TEMP)) {
			ret = spa06_measure(dev, SPA06_MEAS_CTRL_PRS, SPA06_MEAS_CFG_PRS_RDY,
					    data->prs_prc);
		}
	} else if (!data->have_results) {
		ret = spa06_wait_ready(dev, SPA06_MEAS_CFG_PRS_RDY | SPA06_MEAS_CFG_TMP_RDY,
				       spa06_first_results_timeout(data));
		data->have_results = (ret == 0);
	}

	if (ret < 0) {
		LOG_ERR("Measurement failed: %d", ret);
		return ret;
	}

	ret = spa06_reg_read(dev, SPA06_REG_PSR_B2, buf, sizeof(buf));
	if (ret < 0) {
		LOG_ERR("Failed to read results: %d", ret);
		return ret;
	}

	if (chan != SENSOR_CHAN_AMBIENT_TEMP) {
		data->raw_prs = sign_extend(sys_get_be24(&buf[0]), 23);
		data->raw_prs_prc = data->prs_prc;
	}
	data->raw_tmp = sign_extend(sys_get_be24(&buf[3]), 23);
	data->raw_tmp_prc = data->tmp_prc;

	return 0;
}

/* Temperature in micro degrees Celsius: c0 / 2 + c1 * Traw / kT */
int64_t spa06_comp_temp_micro(const struct spa06_coef *coef, int32_t raw_tmp, uint8_t tmp_prc)
{
	return ((int64_t)coef->c0 * 500000) +
	       (((int64_t)coef->c1 * raw_tmp * 1000000) / spa06_scale_factor[tmp_prc]);
}

/*
 * Evaluate a polynomial in raw / k, with coefficients given from the highest
 * order down, and return it multiplied by SPA06_FRAC_SCALE. Dividing by k at
 * each step keeps full precision; with 24-bit raw values and the coefficient
 * widths of this sensor, intermediate values stay below 2^62.
 */
static int64_t spa06_poly(const int32_t *coef, size_t len, int32_t raw, int32_t k)
{
	int64_t acc = 0;

	for (size_t i = 0; i < len; i++) {
		acc = ((int64_t)coef[i] * SPA06_FRAC_SCALE) + ((acc * raw) / k);
	}

	return acc;
}

/*
 * Pressure in micro-kilopascals, from the pressure in pascals:
 *   c00 + Psc * (c10 + Psc * (c20 + Psc * (c30 + Psc * c40)))
 *   + Tsc * (c01 + Psc * (c11 + Psc * (c21 + Psc * c31)))
 * with Psc = Praw / kP and Tsc = Traw / kT.
 */
int64_t spa06_comp_press_micro(const struct spa06_coef *coef, int32_t raw_prs, uint8_t prs_prc,
			       int32_t raw_tmp, uint8_t tmp_prc)
{
	const int32_t prs_coef[] = {coef->c40, coef->c30, coef->c20, coef->c10, coef->c00};
	const int32_t tmp_coef[] = {coef->c31, coef->c21, coef->c11, coef->c01};
	int32_t kp = spa06_scale_factor[prs_prc];
	int32_t kt = spa06_scale_factor[tmp_prc];
	int64_t press;

	press = spa06_poly(prs_coef, ARRAY_SIZE(prs_coef), raw_prs, kp) +
		((spa06_poly(tmp_coef, ARRAY_SIZE(tmp_coef), raw_prs, kp) * raw_tmp) / kt);

	/* One pascal is 1000 micro-kilopascals */
	return (press * 1000) / SPA06_FRAC_SCALE;
}

int32_t spa06_micro_to_q31(int64_t micro, int8_t shift)
{
	const int64_t limit = (int64_t)1000000 << shift;

	micro = CLAMP(micro, -limit, limit);

	return (int32_t)CLAMP((micro * ((int64_t)1 << (31 - shift))) / 1000000, INT32_MIN,
			      INT32_MAX);
}

static int spa06_channel_get(const struct device *dev, enum sensor_channel chan,
			     struct sensor_value *val)
{
	const struct spa06_data *data = dev->data;
	int64_t micro;

	switch (chan) {
	case SENSOR_CHAN_AMBIENT_TEMP:
		micro = spa06_comp_temp_micro(&data->coef, data->raw_tmp, data->raw_tmp_prc);
		break;
	case SENSOR_CHAN_PRESS:
		micro = spa06_comp_press_micro(&data->coef, data->raw_prs, data->raw_prs_prc,
					       data->raw_tmp, data->raw_tmp_prc);
		break;
	default:
		return -ENOTSUP;
	}

	return sensor_value_from_micro(val, micro);
}

static int spa06_apply(const struct device *dev, uint8_t prs_prc, uint8_t tmp_prc, uint8_t prs_odr,
		       uint8_t tmp_odr)
{
	struct spa06_data *data = dev->data;

	if (!spa06_timing_ok(prs_odr, prs_prc, tmp_odr, tmp_prc)) {
		LOG_ERR("Rates and oversampling exceed the available measurement time");
		return -EINVAL;
	}

	data->prs_prc = prs_prc;
	data->tmp_prc = tmp_prc;
	data->prs_odr = prs_odr;
	data->tmp_odr = tmp_odr;

	/* While suspended, the settings are applied on resume */
	if (!spa06_is_active(dev)) {
		return 0;
	}

	return spa06_configure(dev);
}

static int spa06_set_oversampling(const struct device *dev, enum sensor_channel chan,
				  const struct sensor_value *val)
{
	struct spa06_data *data = dev->data;
	uint8_t prs_prc = data->prs_prc;
	uint8_t tmp_prc = data->tmp_prc;
	uint8_t prc;

	if ((val->val2 != 0) || (val->val1 < 1) || (val->val1 > 128) ||
	    !IS_POWER_OF_TWO((uint32_t)val->val1)) {
		return -EINVAL;
	}

	prc = (uint8_t)u32_count_trailing_zeros((uint32_t)val->val1);

	switch (chan) {
	case SENSOR_CHAN_PRESS:
		prs_prc = prc;
		break;
	case SENSOR_CHAN_AMBIENT_TEMP:
		tmp_prc = prc;
		break;
	case SENSOR_CHAN_ALL:
		prs_prc = prc;
		tmp_prc = prc;
		break;
	default:
		return -ENOTSUP;
	}

	return spa06_apply(dev, prs_prc, tmp_prc, data->prs_odr, data->tmp_odr);
}

/* Find the lowest rate at or above the requested one */
static int spa06_odr_from_micro(int64_t micro_hz, bool temperature, uint8_t *odr)
{
	uint8_t best = SPA06_ODR_NONE;

	for (uint8_t i = 0U; i < ARRAY_SIZE(spa06_odr_uhz); i++) {
		if (temperature && (i == SPA06_ODR_PRS_ONLY)) {
			continue;
		}

		if ((spa06_odr_uhz[i] >= micro_hz) &&
		    ((best == SPA06_ODR_NONE) || (spa06_odr_uhz[i] < spa06_odr_uhz[best]))) {
			best = i;
		}
	}

	if (best == SPA06_ODR_NONE) {
		return -EINVAL;
	}

	*odr = best;

	return 0;
}

static int spa06_set_odr(const struct device *dev, enum sensor_channel chan,
			 const struct sensor_value *val)
{
	struct spa06_data *data = dev->data;
	int64_t micro_hz = sensor_value_to_micro(val);
	uint8_t prs_odr = data->prs_odr;
	uint8_t tmp_odr = data->tmp_odr;
	int ret;

	if (micro_hz < 0) {
		return -EINVAL;
	}

	switch (chan) {
	case SENSOR_CHAN_PRESS:
	case SENSOR_CHAN_ALL:
		if (micro_hz == 0) {
			/* Return to on-demand measurements */
			prs_odr = SPA06_ODR_NONE;
			break;
		}

		ret = spa06_odr_from_micro(micro_hz, false, &prs_odr);
		if ((ret == 0) && (chan == SENSOR_CHAN_ALL)) {
			ret = spa06_odr_from_micro(micro_hz, true, &tmp_odr);
		}
		if (ret < 0) {
			return ret;
		}
		break;
	case SENSOR_CHAN_AMBIENT_TEMP:
		if (micro_hz == 0) {
			return -EINVAL;
		}

		ret = spa06_odr_from_micro(micro_hz, true, &tmp_odr);
		if (ret < 0) {
			return ret;
		}
		break;
	default:
		return -ENOTSUP;
	}

	/* Interrupts and streaming rely on continuous measurements */
	if ((prs_odr == SPA06_ODR_NONE) && (data->int_cfg != 0U)) {
		return -EBUSY;
	}

	return spa06_apply(dev, data->prs_prc, data->tmp_prc, prs_odr, tmp_odr);
}

static int spa06_attr_set(const struct device *dev, enum sensor_channel chan,
			  enum sensor_attribute attr, const struct sensor_value *val)
{
	switch (attr) {
	case SENSOR_ATTR_OVERSAMPLING:
		return spa06_set_oversampling(dev, chan, val);
	case SENSOR_ATTR_SAMPLING_FREQUENCY:
		return spa06_set_odr(dev, chan, val);
	default:
		return -ENOTSUP;
	}
}

static int spa06_attr_get(const struct device *dev, enum sensor_channel chan,
			  enum sensor_attribute attr, struct sensor_value *val)
{
	const struct spa06_data *data = dev->data;
	uint8_t prc;
	uint8_t odr;

	switch (chan) {
	case SENSOR_CHAN_PRESS:
		prc = data->prs_prc;
		odr = data->prs_odr;
		break;
	case SENSOR_CHAN_AMBIENT_TEMP:
		prc = data->tmp_prc;
		odr = (data->prs_odr == SPA06_ODR_NONE) ? SPA06_ODR_NONE : data->tmp_odr;
		break;
	default:
		return -ENOTSUP;
	}

	switch (attr) {
	case SENSOR_ATTR_OVERSAMPLING:
		val->val1 = (int32_t)BIT(prc);
		val->val2 = 0;
		return 0;
	case SENSOR_ATTR_SAMPLING_FREQUENCY:
		return sensor_value_from_micro(val,
					       (odr == SPA06_ODR_NONE) ? 0 : spa06_odr_uhz[odr]);
	default:
		return -ENOTSUP;
	}
}

static void spa06_parse_coef(const uint8_t *buf, struct spa06_coef *coef)
{
	coef->c0 = sign_extend(((uint32_t)buf[0] << 4) | ((uint32_t)buf[1] >> 4), 11);
	coef->c1 = sign_extend((((uint32_t)buf[1] & 0x0FU) << 8) | buf[2], 11);
	coef->c00 = sign_extend(
		((uint32_t)buf[3] << 12) | ((uint32_t)buf[4] << 4) | ((uint32_t)buf[5] >> 4), 19);
	coef->c10 = sign_extend((((uint32_t)buf[5] & 0x0FU) << 16) | sys_get_be16(&buf[6]), 19);
	coef->c01 = (int16_t)sys_get_be16(&buf[8]);
	coef->c11 = (int16_t)sys_get_be16(&buf[10]);
	coef->c20 = (int16_t)sys_get_be16(&buf[12]);
	coef->c21 = (int16_t)sys_get_be16(&buf[14]);
	coef->c30 = (int16_t)sys_get_be16(&buf[16]);
	coef->c31 = sign_extend(((uint32_t)buf[18] << 4) | ((uint32_t)buf[19] >> 4), 11);
	coef->c40 = sign_extend((((uint32_t)buf[19] & 0x0FU) << 8) | buf[20], 11);
}

/* Check the sensor, reset it and read its calibration coefficients */
static int spa06_chip_init(const struct device *dev)
{
	struct spa06_data *data = dev->data;
	uint8_t coef[SPA06_COEF_LEN];
	uint8_t id;
	int ret;

	ret = spa06_reg_read(dev, SPA06_REG_ID, &id, 1);
	if (ret < 0) {
		LOG_ERR("Failed to read ID: %d", ret);
		return ret;
	}

	if (FIELD_GET(SPA06_ID_PROD_MASK, id) != SPA06_PROD_ID) {
		LOG_ERR("Unexpected ID 0x%02x", id);
		return -ENODEV;
	}

	/* Start from the power-on state, the sensor may have been left measuring */
	ret = spa06_reg_write(dev, SPA06_REG_RESET, SPA06_RESET_SOFT_RST);
	if (ret < 0) {
		LOG_ERR("Soft reset failed: %d", ret);
		return ret;
	}

	ret = spa06_wait_ready(dev, SPA06_MEAS_CFG_COEF_RDY | SPA06_MEAS_CFG_SENSOR_RDY,
			       SPA06_STARTUP_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("Sensor not ready after reset: %d", ret);
		return ret;
	}

	ret = spa06_reg_read(dev, SPA06_REG_COEF, coef, sizeof(coef));
	if (ret < 0) {
		LOG_ERR("Failed to read coefficients: %d", ret);
		return ret;
	}

	spa06_parse_coef(coef, &data->coef);

	return 0;
}

static int spa06_pm_action(const struct device *dev, enum pm_device_action action)
{
	int ret;

	switch (action) {
	case PM_DEVICE_ACTION_TURN_ON:
		/* Registers are lost without power, the reset leaves the sensor in standby */
		return spa06_chip_init(dev);
	case PM_DEVICE_ACTION_RESUME:
		ret = spa06_configure(dev);
#ifdef CONFIG_SPA06_TRIGGER
		if (ret == 0) {
			ret = spa06_trigger_enable(dev, true);
		}
#endif
		return ret;
	case PM_DEVICE_ACTION_SUSPEND:
#ifdef CONFIG_SPA06_TRIGGER
		ret = spa06_trigger_enable(dev, false);
		if (ret < 0) {
			return ret;
		}
#endif
		/* Standby stops measurements and keeps the configuration and results */
		return spa06_reg_write(dev, SPA06_REG_MEAS_CFG, SPA06_MEAS_CTRL_IDLE);
	case PM_DEVICE_ACTION_TURN_OFF:
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int spa06_init(const struct device *dev)
{
	const struct spa06_config *cfg = dev->config;
	struct spa06_data *data = dev->data;
	int ret;

	ret = cfg->bus_io->check(&cfg->bus);
	if (ret < 0) {
		LOG_ERR("Bus not ready");
		return ret;
	}

	if (!spa06_timing_ok(cfg->prs_odr, cfg->prs_prc, cfg->tmp_odr, cfg->tmp_prc)) {
		LOG_ERR("Rates and oversampling exceed the available measurement time");
		return -EINVAL;
	}

	data->prs_prc = cfg->prs_prc;
	data->tmp_prc = cfg->tmp_prc;
	data->prs_odr = cfg->prs_odr;
	data->tmp_odr = cfg->tmp_odr;
	data->raw_prs_prc = cfg->prs_prc;
	data->raw_tmp_prc = cfg->tmp_prc;

#ifdef CONFIG_SPA06_TRIGGER
	ret = spa06_trigger_init(dev);
	if (ret < 0) {
		LOG_ERR("Failed to set up the interrupt: %d", ret);
		return ret;
	}
#endif

	return pm_device_driver_init(dev, spa06_pm_action);
}

static DEVICE_API(sensor, spa06_api) = {
	.sample_fetch = spa06_sample_fetch,
	.channel_get = spa06_channel_get,
	.attr_set = spa06_attr_set,
	.attr_get = spa06_attr_get,
#ifdef CONFIG_SPA06_TRIGGER
	.trigger_set = spa06_trigger_set,
#endif
#ifdef CONFIG_SENSOR_ASYNC_API
	.submit = spa06_submit,
	.get_decoder = spa06_get_decoder,
#endif
};

#define SPA06_CONFIG_SPI(inst)                                                                     \
	.bus.spi = SPI_DT_SPEC_INST_GET(inst, SPA06_SPI_OPERATION), .bus_io = &spa06_bus_io_spi,

#define SPA06_CONFIG_I2C(inst) .bus.i2c = I2C_DT_SPEC_INST_GET(inst), .bus_io = &spa06_bus_io_i2c,

#define SPA06_DEFINE(inst)                                                                         \
	static struct spa06_data spa06_data_##inst;                                                \
                                                                                                   \
	static const struct spa06_config spa06_config_##inst = {                                   \
		COND_CODE_1(DT_INST_ON_BUS(inst, spi), (SPA06_CONFIG_SPI(inst)),                   \
			    (SPA06_CONFIG_I2C(inst)))                                              \
		IF_ENABLED(CONFIG_SPA06_TRIGGER,                                                   \
			   (.int_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, int_gpios, {0}),))          \
		.prs_prc = DT_INST_ENUM_IDX(inst, pressure_oversampling),                          \
		.tmp_prc = DT_INST_ENUM_IDX(inst, temperature_oversampling),                       \
		.prs_odr = DT_INST_PROP_OR(inst, pressure_odr, SPA06_ODR_NONE),                    \
		.tmp_odr = DT_INST_PROP(inst, temperature_odr),                                    \
	};                                                                                         \
                                                                                                   \
	PM_DEVICE_DT_INST_DEFINE(inst, spa06_pm_action);                                           \
                                                                                                   \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, spa06_init, PM_DEVICE_DT_INST_GET(inst),                \
				     &spa06_data_##inst, &spa06_config_##inst, POST_KERNEL,        \
				     CONFIG_SENSOR_INIT_PRIORITY, &spa06_api);

DT_INST_FOREACH_STATUS_OKAY(SPA06_DEFINE)
