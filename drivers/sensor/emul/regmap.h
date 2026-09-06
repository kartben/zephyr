/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_DRIVERS_SENSOR_EMUL_REGMAP_H_
#define ZEPHYR_DRIVERS_SENSOR_EMUL_REGMAP_H_

#include <zephyr/drivers/emul_sensor.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/kernel.h>

/* Registers are indexed by their datasheet address; bytes == 0 means reserved. */
struct emul_regmap_register {
	uint8_t bytes;
	uint32_t reset;
	uint32_t write_mask;
	uint32_t clear_on_read;
};

/* physical = signed_raw * lsb + offset, in Zephyr sensor channel units. */
struct emul_regmap_channel {
	enum sensor_channel channel;
	uint8_t reg;
	double lsb;
	double offset;
	double min;
	double max;
	uint8_t shift;
};

struct emul_regmap_config {
	const struct emul_regmap_register *registers;
	size_t register_count;
	const struct emul_regmap_channel *channels;
	size_t channel_count;
	bool byte_addressed;
	bool little_endian;
	/* Optional register bit controlling byte address increment. */
	uint8_t increment_reg;
	uint8_t increment_mask;
	/* Bus callbacks run after each read byte or completed register write. */
	void (*read)(const struct emul *target, uint8_t reg);
	void (*write)(const struct emul *target, uint8_t reg, uint32_t old);
	/* Adjust the sample encoding for the currently selected range/resolution. */
	void (*channel)(const struct emul *target, struct emul_regmap_channel *channel);
	/* Return true to store value; false to suppress or handle it in the callback. */
	bool (*sample)(const struct emul *target, uint8_t reg, uint32_t value);
};

struct emul_regmap_data {
	struct k_mutex lock;
	uint32_t *values;
	double *inputs;
	bool *valid;
	uint8_t pointer;
};

extern const struct i2c_emul_api emul_regmap_i2c_api;
extern const struct emul_sensor_driver_api emul_regmap_sensor_api;

int emul_regmap_init(const struct emul *target, const struct device *parent);
void emul_regmap_reset(const struct emul *target);
/* Complete conversions using the last injected inputs; called with lock held. */
void emul_regmap_convert(const struct emul *target);

/* Pass arrays separately so their sizes remain compile-time constants. */
#define EMUL_REGMAP_DT_INST_DEFINE(inst, config, regs, channels)                               \
	static uint32_t values_##inst[ARRAY_SIZE(regs)];                                       \
	static double inputs_##inst[ARRAY_SIZE(channels)];                                     \
	static bool valid_##inst[ARRAY_SIZE(channels)];                                        \
	static struct emul_regmap_data data_##inst = {                                         \
		.values = values_##inst, .inputs = inputs_##inst, .valid = valid_##inst,       \
	};                                                                                     \
	EMUL_DT_INST_DEFINE(inst, emul_regmap_init, &data_##inst, &config,                     \
			    &emul_regmap_i2c_api, &emul_regmap_sensor_api)

#endif
