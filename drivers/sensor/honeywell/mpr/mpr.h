/*
 * Copyright (c) 2020 Sven Herrmann
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_SENSOR_MPR_H_
#define ZEPHYR_DRIVERS_SENSOR_MPR_H_

#include <zephyr/devicetree.h>

#define MPR_BUS_I2C DT_ANY_INST_ON_BUS_STATUS_OKAY(i2c)
#define MPR_BUS_SPI DT_ANY_INST_ON_BUS_STATUS_OKAY(spi)

#if MPR_BUS_I2C
#include <zephyr/drivers/i2c.h>
#endif
#if MPR_BUS_SPI
#include <zephyr/drivers/spi.h>
#endif

/* MPR output measurement command (I2C and SPI) */
#define MPR_OUTPUT_MEASUREMENT_COMMAND (0xAA)

/* MPR SPI read command */
#define MPR_SPI_READ_COMMAND (0xF0)

/* MPR SPI operation: mode 0, MSB first */
#define MPR_SPI_OPERATION (SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER)

/* MPR status byte masks */
#define MPR_STATUS_MASK_MATH_SATURATION       (0x01)
#define MPR_STATUS_MASK_INTEGRITY_TEST_FAILED (0x04)
#define MPR_STATUS_MASK_BUSY                  (0x20)
#define MPR_STATUS_MASK_POWER_ON              (0x40)

/* MPR register read maximum retries */
#ifndef MPR_REG_READ_MAX_RETRIES
#define MPR_REG_READ_MAX_RETRIES (3)
#endif

/* MPR register read data conversion delay [ms] */
#ifndef MPR_REG_READ_DATA_CONV_DELAY_MS
#define MPR_REG_READ_DATA_CONV_DELAY_MS (5)
#endif

union mpr_bus {
#if MPR_BUS_I2C
	struct i2c_dt_spec i2c;
#endif
#if MPR_BUS_SPI
	struct spi_dt_spec spi;
#endif
};

typedef int (*mpr_bus_check_fn)(const union mpr_bus *bus);
typedef int (*mpr_bus_request_fn)(const union mpr_bus *bus);
typedef int (*mpr_bus_read_fn)(const union mpr_bus *bus, uint8_t *buf, size_t len);

struct mpr_bus_io {
	mpr_bus_check_fn check;
	mpr_bus_request_fn request;
	mpr_bus_read_fn read;
};

#if MPR_BUS_I2C
extern const struct mpr_bus_io mpr_bus_io_i2c;
#endif
#if MPR_BUS_SPI
extern const struct mpr_bus_io mpr_bus_io_spi;
#endif

struct mpr_data {
	uint32_t reg_val;
};

struct mpr_config {
	union mpr_bus bus;
	const struct mpr_bus_io *bus_io;
};

#endif /* ZEPHYR_DRIVERS_SENSOR_MPR_H_ */
