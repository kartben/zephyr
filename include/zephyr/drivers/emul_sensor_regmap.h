/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_EMUL_SENSOR_REGMAP_H_
#define ZEPHYR_INCLUDE_DRIVERS_EMUL_SENSOR_REGMAP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/util.h>

/**
 * @brief Register map based sensor emulators
 * @defgroup emul_sensor_regmap Register map based sensor emulators
 * @ingroup sensor_emulator_backend
 *
 * Describes an I2C register based sensor as data transcribed from its datasheet: the register
 * table and where each measurement lives in it. The emulator implements the bus protocol and
 * the sensor emulator backend API from that description.
 *
 * @{
 */

/** Register is read only, writes are ignored. */
#define EMUL_SENSOR_REG_RO BIT(0)

/** One row of the datasheet register table. */
struct emul_sensor_reg {
	/** Register address. */
	uint8_t addr;
	/** Register name, used in logs. */
	const char *name;
	/** @ref EMUL_SENSOR_REG_RO or 0. */
	uint8_t flags;
	/** Register width in bytes, 0 for the device default. */
	uint8_t bytes;
	/** Value after reset. */
	uint32_t reset;
	/** Bits that clear themselves after being written (one-shot, soft reset, ...). */
	uint32_t self_clear;
	/** Writable bits, 0 for all of them. */
	uint32_t write_mask;
	/** Bits cleared once the register has been read (data ready, interrupt status, ...). */
	uint32_t clear_on_read;
};

/** A register bit field: register address and mask. */
struct emul_sensor_bits {
	uint8_t reg;
	uint32_t mask;
};

/** Where a raw sample sits in a data word and how it scales to SI units. */
struct emul_sensor_field {
	/** Number of bits. */
	uint8_t bits;
	/** Position of the least significant bit in the data word. */
	uint8_t pos;
	/** SI units per LSB. */
	double lsb;
	/** Measurement range. */
	double min;
	double max;
};

/**
 * One measurement of the sensor.
 *
 * The data word is formed from as many consecutive registers, starting at @a reg, as the field
 * needs, in the byte order of the device. When @a select has a mask, the value of that bit
 * field in its register picks the entry of @a variants that describes the field; members of a
 * variant left at zero inherit the channel value.
 */
struct emul_sensor_channel {
	/** Channel type. */
	enum sensor_channel chan;
	/** Address of the first register of the data word. */
	uint8_t reg;
	/** Two's complement data. */
	bool is_signed;
	/** Number of bits. */
	uint8_t bits;
	/** Position of the least significant bit in the data word. */
	uint8_t pos;
	/** SI units per LSB. */
	double lsb;
	/** SI value of a raw code of 0. */
	double offset;
	/** Measurement range; both 0 to derive it from the field. */
	double min;
	double max;
	/** Configuration field selecting the active variant, mask 0 for none. */
	struct emul_sensor_bits select;
	/** Field description per value of @a select. */
	struct emul_sensor_field variants[8];
	/** Status bits set when a new sample is written. */
	struct emul_sensor_bits ready;
};

/** Sensor description. */
struct emul_sensor_regmap {
	const struct emul_sensor_reg *regs;
	size_t num_regs;
	const struct emul_sensor_channel *channels;
	size_t num_channels;
	/** Default register width in bytes, 0 for 1. */
	uint8_t reg_bytes;
	/** Most significant byte first on the bus. */
	bool big_endian;
	/** Bits of the register address byte that are not part of the address. */
	uint8_t addr_ignore;
};

/** @cond INTERNAL_HIDDEN */
struct emul_sensor_regmap_data {
	uint32_t regs[256];
	uint8_t ptr;
	uint8_t pos;
};

extern const struct i2c_emul_api emul_sensor_regmap_i2c_api;
extern const struct emul_sensor_driver_api emul_sensor_regmap_backend_api;

int emul_sensor_regmap_init(const struct emul *target, const struct device *parent);

#define Z_EMUL_SENSOR_REGMAP_DT_INST_DEFINE(inst, desc)                                            \
	static struct emul_sensor_regmap_data emul_sensor_regmap_data_##inst;                      \
	EMUL_DT_INST_DEFINE(inst, emul_sensor_regmap_init, &emul_sensor_regmap_data_##inst, &desc, \
			    &emul_sensor_regmap_i2c_api, &emul_sensor_regmap_backend_api)

/* Instances on other buses (SPI) are left without an emulator. */
#define Z_EMUL_SENSOR_REGMAP_DT_INST_I2C(inst, desc)                                               \
	IF_ENABLED(DT_INST_ON_BUS(inst, i2c), (Z_EMUL_SENSOR_REGMAP_DT_INST_DEFINE(inst, desc);))
/** @endcond */

/**
 * @brief Define an emulator for every enabled I2C instance of `DT_DRV_COMPAT`
 *
 * @param _regs Array of @ref emul_sensor_reg
 * @param _channels Array of @ref emul_sensor_channel
 * @param ... Remaining @ref emul_sensor_regmap initializers, for example `.reg_bytes = 2`
 */
#define EMUL_SENSOR_REGMAP_DEFINE(_regs, _channels, ...)                                           \
	static const struct emul_sensor_regmap UTIL_CAT(emul_sensor_regmap_, DT_DRV_COMPAT) = {    \
		.regs = _regs,                                                                     \
		.num_regs = ARRAY_SIZE(_regs),                                                     \
		.channels = _channels,                                                             \
		.num_channels = ARRAY_SIZE(_channels),                                             \
		__VA_ARGS__};                                                                      \
	DT_INST_FOREACH_STATUS_OKAY_VARGS(Z_EMUL_SENSOR_REGMAP_DT_INST_I2C,                        \
					  UTIL_CAT(emul_sensor_regmap_, DT_DRV_COMPAT))

/**
 * @brief Read a register of the emulated sensor
 *
 * @param target Emulator instance
 * @param addr Register address
 * @return Register value
 */
uint32_t emul_sensor_regmap_get_reg(const struct emul *target, uint8_t addr);

/**
 * @brief Write a register of the emulated sensor, bypassing the bus
 *
 * @param target Emulator instance
 * @param addr Register address
 * @param val Value to write
 */
void emul_sensor_regmap_set_reg(const struct emul *target, uint8_t addr, uint32_t val);

/** @} */

#endif /* ZEPHYR_INCLUDE_DRIVERS_EMUL_SENSOR_REGMAP_H_ */
