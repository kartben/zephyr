/* mpr.c - Driver for Honeywell MPR pressure sensor series */

/*
 * Copyright (c) 2020 Sven Herrmann
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT honeywell_mpr

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include "mpr.h"
#include "mpr_configuration.h"

LOG_MODULE_REGISTER(MPR, CONFIG_SENSOR_LOG_LEVEL);

#if MPR_BUS_I2C
static int mpr_bus_check_i2c(const union mpr_bus *bus)
{
	return i2c_is_ready_dt(&bus->i2c) ? 0 : -ENODEV;
}

static int mpr_bus_request_i2c(const union mpr_bus *bus)
{
	uint8_t write_buf[] = { MPR_OUTPUT_MEASUREMENT_COMMAND, 0x00, 0x00 };

	return i2c_write_dt(&bus->i2c, write_buf, sizeof(write_buf));
}

static int mpr_bus_read_i2c(const union mpr_bus *bus, uint8_t *buf, size_t len)
{
	return i2c_read_dt(&bus->i2c, buf, len);
}

const struct mpr_bus_io mpr_bus_io_i2c = {
	.check = mpr_bus_check_i2c,
	.request = mpr_bus_request_i2c,
	.read = mpr_bus_read_i2c,
};
#endif /* MPR_BUS_I2C */

#if MPR_BUS_SPI
static int mpr_bus_check_spi(const union mpr_bus *bus)
{
	return spi_is_ready_dt(&bus->spi) ? 0 : -ENODEV;
}

static int mpr_bus_request_spi(const union mpr_bus *bus)
{
	uint8_t cmd[] = { MPR_OUTPUT_MEASUREMENT_COMMAND, 0x00, 0x00 };
	struct spi_buf tx_buf = { .buf = cmd, .len = sizeof(cmd) };
	struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };

	return spi_write_dt(&bus->spi, &tx);
}

static int mpr_bus_read_spi(const union mpr_bus *bus, uint8_t *buf, size_t len)
{
	/*
	 * In full-duplex SPI, 4 bytes must be clocked out to receive 4 bytes:
	 * byte 0: read command (0xF0), bytes 1-3: dummy bytes to clock out
	 * the 3-byte pressure value from the sensor.
	 */
	uint8_t cmd[4] = { MPR_SPI_READ_COMMAND, 0x00, 0x00, 0x00 };
	struct spi_buf tx_buf = { .buf = cmd, .len = sizeof(cmd) };
	struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
	struct spi_buf rx_buf = { .buf = buf, .len = len };
	struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };

	return spi_transceive_dt(&bus->spi, &tx, &rx);
}

const struct mpr_bus_io mpr_bus_io_spi = {
	.check = mpr_bus_check_spi,
	.request = mpr_bus_request_spi,
	.read = mpr_bus_read_spi,
};
#endif /* MPR_BUS_SPI */

static int mpr_init(const struct device *dev)
{
	const struct mpr_config *cfg = dev->config;

	return cfg->bus_io->check(&cfg->bus);
}

static int mpr_read_reg(const struct device *dev)
{
	struct mpr_data *data = dev->data;
	const struct mpr_config *cfg = dev->config;

	uint8_t read_buf[4] = { 0x0 };

	int rc = cfg->bus_io->request(&cfg->bus);

	if (rc < 0) {
		return rc;
	}

	uint8_t retries = MPR_REG_READ_MAX_RETRIES;

	for (; retries > 0; retries--) {
		k_sleep(K_MSEC(MPR_REG_READ_DATA_CONV_DELAY_MS));

		rc = cfg->bus_io->read(&cfg->bus, read_buf, sizeof(read_buf));
		if (rc < 0) {
			return rc;
		}

		if (!(*read_buf & MPR_STATUS_MASK_POWER_ON)
			|| (*read_buf & MPR_STATUS_MASK_INTEGRITY_TEST_FAILED)
			|| (*read_buf & MPR_STATUS_MASK_MATH_SATURATION)) {
			return -EIO;
		}

		if (!(*read_buf & MPR_STATUS_MASK_BUSY)) {
			break;
		}
	}

	if (retries == 0) {
		return -EIO;
	}

	data->reg_val = (read_buf[1] << 16)
			| (read_buf[2] << 8)
			|  read_buf[3];

	return 0;
}

/*            (reg_value - out_min) * (p_max - p_min)
 * pressure = --------------------------------------- + p_min
 *                     out_max - out_min
 *
 * returns pressure [kPa] * 10^6
 */
static inline void mpr_convert_reg(const uint32_t *reg, uint64_t *value)
{
	if (*reg > MPR_OUTPUT_MIN) {
		*value = (uint64_t)(*reg - MPR_OUTPUT_MIN) * (MPR_P_MAX - MPR_P_MIN);
		*value *= MPR_CONVERSION_FACTOR;
		*value /= MPR_OUTPUT_RANGE;
		*value += MPR_P_MIN;
	} else {
		*value = MPR_P_MIN;
	}
}

static int mpr_sample_fetch(const struct device *dev,
			    enum sensor_channel chan)
{
	__ASSERT_NO_MSG(chan == SENSOR_CHAN_ALL || chan == SENSOR_CHAN_PRESS);

	return mpr_read_reg(dev);
}

static int mpr_channel_get(const struct device *dev,
			   enum sensor_channel chan,
			   struct sensor_value *val)
{
	const struct mpr_data *data = dev->data;

	__ASSERT_NO_MSG(chan == SENSOR_CHAN_PRESS);

	if (chan != SENSOR_CHAN_PRESS) {
		return -ENOTSUP;
	}

	uint64_t value;

	mpr_convert_reg(&data->reg_val, &value);

	val->val1 = value / 1000000;
	val->val2 = value % 1000000;

	return 0;
}

static DEVICE_API(sensor, mpr_api_funcs) = {
	.sample_fetch = mpr_sample_fetch,
	.channel_get = mpr_channel_get,
};

/* Initializes a struct mpr_config for an instance on an SPI bus. */
#define MPR_CONFIG_SPI(inst)							\
	{								\
		.bus.spi = SPI_DT_SPEC_INST_GET(inst, MPR_SPI_OPERATION, 0),	\
		.bus_io = &mpr_bus_io_spi,					\
	}

/* Initializes a struct mpr_config for an instance on an I2C bus. */
#define MPR_CONFIG_I2C(inst)							\
	{								\
		.bus.i2c = I2C_DT_SPEC_INST_GET(inst),				\
		.bus_io = &mpr_bus_io_i2c,					\
	}

#define MPR_DEFINE(inst)								\
	static struct mpr_data mpr_data_##inst;						\
											\
	static const struct mpr_config mpr_config_##inst =				\
		COND_CODE_1(DT_INST_ON_BUS(inst, spi),					\
			    (MPR_CONFIG_SPI(inst)),					\
			    (MPR_CONFIG_I2C(inst)));					\
											\
	SENSOR_DEVICE_DT_INST_DEFINE(inst, mpr_init, NULL,				\
			      &mpr_data_##inst, &mpr_config_##inst, POST_KERNEL,	\
			      CONFIG_SENSOR_INIT_PRIORITY, &mpr_api_funcs);		\

DT_INST_FOREACH_STATUS_OKAY(MPR_DEFINE)
