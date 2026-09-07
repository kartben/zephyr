/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_DRIVERS_SENSOR_EMUL_REGMAP_H_
#define ZEPHYR_DRIVERS_SENSOR_EMUL_REGMAP_H_

#include <zephyr/drivers/emul_sensor.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/kernel.h>

struct emul_regmap_field {
	uint8_t reg;
	uint32_t mask;
	uint32_t value;
};

struct emul_regmap_range {
	double lsb;
	uint8_t shift;
};

/* Registers are indexed by their datasheet address; bytes == 0 means reserved. */
struct emul_regmap_register {
	uint8_t bytes;
	uint32_t reset;
	uint32_t write_mask;
	uint32_t clear_on_read;
	struct emul_regmap_field read_clears;
	uint32_t self_clear;
	uint32_t reset_on_write;
	uint32_t convert_on_write;
	bool requires_standby;
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
	struct emul_regmap_field range_select;
	const struct emul_regmap_range *ranges;
	size_t range_count;
	bool range_limits;
	struct emul_regmap_field disabled;
	struct emul_regmap_field ready;
	uint32_t overrun;
};

struct emul_regmap_config {
	const struct emul_regmap_register *registers;
	size_t register_count;
	const struct emul_regmap_channel *channels;
	size_t channel_count;
	bool byte_addressed;
	bool little_endian;
	/* Sampling stops when a nonempty field matches its value. */
	struct emul_regmap_field disabled;
	struct emul_regmap_field block_update;

	/* Optional register bit controlling byte address increment. */
	uint8_t increment_reg;
	uint8_t increment_mask;
	/* Bus callbacks run after each read byte or completed register write. */
	void (*read)(const struct emul *target, uint8_t reg);
	void (*write)(const struct emul *target, uint8_t reg, uint32_t old);
	/* Return true to store value; false to suppress or handle it in the callback. */
	bool (*sample)(const struct emul *target, uint8_t reg, uint32_t value);
};

struct emul_regmap_data {
	struct k_mutex lock;
	uint32_t *values;
	double *inputs;
	bool *valid;
	uint8_t pointer;
	bool force_conversion;
};

extern const struct i2c_emul_api emul_regmap_i2c_api;
extern const struct emul_sensor_driver_api emul_regmap_sensor_api;

int emul_regmap_init(const struct emul *target, const struct device *parent);
void emul_regmap_reset(const struct emul *target);
/* Complete conversions using the last injected inputs; called with lock held. */
void emul_regmap_convert(const struct emul *target);
void emul_regmap_channel_config(const struct emul *target, struct emul_regmap_channel *channel);

/* Pass arrays separately so their sizes remain compile-time constants. */
#define EMUL_REGMAP_DT_INST_DEFINE(inst, config, regs, channels)                                \
	static uint32_t values_##inst[ARRAY_SIZE(regs)];                                        \
	static double inputs_##inst[ARRAY_SIZE(channels)];                                      \
	static bool valid_##inst[ARRAY_SIZE(channels)];                                         \
	static struct emul_regmap_data data_##inst = {                                          \
		.values = values_##inst, .inputs = inputs_##inst, .valid = valid_##inst,        \
	};                                                                                      \
	EMUL_DT_INST_DEFINE(inst, emul_regmap_init, &data_##inst, &config,                      \
			    &emul_regmap_i2c_api, &emul_regmap_sensor_api);

#define EMUL_REGMAP_MODEL(regs, chans, ...)                                                     \
	static const struct emul_regmap_config config = {                                       \
		.registers = regs, .register_count = ARRAY_SIZE(regs),                          \
		.channels = chans, .channel_count = ARRAY_SIZE(chans), __VA_ARGS__              \
	};                                                                                      \
	DT_INST_FOREACH_STATUS_OKAY_VARGS(EMUL_REGMAP_DT_INST_DEFINE, config, regs, chans)

#endif
