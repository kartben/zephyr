/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "spa06.h"

#if SPA06_BUS_I2C
static int spa06_i2c_check(const union spa06_bus *bus)
{
	return i2c_is_ready_dt(&bus->i2c) ? 0 : -ENODEV;
}

static int spa06_i2c_read(const union spa06_bus *bus, uint8_t reg, uint8_t *buf, size_t len)
{
	return i2c_burst_read_dt(&bus->i2c, reg, buf, len);
}

static int spa06_i2c_write(const union spa06_bus *bus, uint8_t reg, uint8_t val)
{
	return i2c_reg_write_byte_dt(&bus->i2c, reg, val);
}

const struct spa06_bus_io spa06_bus_io_i2c = {
	.check = spa06_i2c_check,
	.read = spa06_i2c_read,
	.write = spa06_i2c_write,
};
#endif /* SPA06_BUS_I2C */

#if SPA06_BUS_SPI
static int spa06_spi_check(const union spa06_bus *bus)
{
	return spi_is_ready_dt(&bus->spi) ? 0 : -ENODEV;
}

static int spa06_spi_read(const union spa06_bus *bus, uint8_t reg, uint8_t *buf, size_t len)
{
	uint8_t addr = reg | SPA06_SPI_READ;
	const struct spi_buf tx_buf = {
		.buf = &addr,
		.len = 1,
	};
	const struct spi_buf_set tx = {
		.buffers = &tx_buf,
		.count = 1,
	};
	const struct spi_buf rx_buf[] = {
		{
			.buf = NULL,
			.len = 1,
		},
		{
			.buf = buf,
			.len = len,
		},
	};
	const struct spi_buf_set rx = {
		.buffers = rx_buf,
		.count = ARRAY_SIZE(rx_buf),
	};

	return spi_transceive_dt(&bus->spi, &tx, &rx);
}

static int spa06_spi_write(const union spa06_bus *bus, uint8_t reg, uint8_t val)
{
	uint8_t cmd[] = {reg & (uint8_t)~SPA06_SPI_READ, val};
	const struct spi_buf tx_buf = {
		.buf = cmd,
		.len = sizeof(cmd),
	};
	const struct spi_buf_set tx = {
		.buffers = &tx_buf,
		.count = 1,
	};

	return spi_write_dt(&bus->spi, &tx);
}

const struct spa06_bus_io spa06_bus_io_spi = {
	.check = spa06_spi_check,
	.read = spa06_spi_read,
	.write = spa06_spi_write,
};
#endif /* SPA06_BUS_SPI */
