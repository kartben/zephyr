/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "regmap.h"

#include <math.h>

void emul_regmap_reset(const struct emul *target)
{
	const struct emul_regmap_config *cfg = target->cfg;
	struct emul_regmap_data *data = target->data;

	k_mutex_lock(&data->lock, K_FOREVER);
	for (size_t i = 0; i < cfg->register_count; i++) {
		data->values[i] = cfg->registers[i].reset;
	}
	data->pointer = 0;
	k_mutex_unlock(&data->lock);
}

static void convert_channel(const struct emul *target, size_t index)
{
	const struct emul_regmap_config *cfg = target->cfg;
	struct emul_regmap_data *data = target->data;
	struct emul_regmap_channel ch = cfg->channels[index];
	int64_t raw;
	uint32_t value;

	if (!data->valid[index]) {
		return;
	}
	if (cfg->channel != NULL) {
		cfg->channel(target, &ch);
	}
	raw = (int64_t)round((data->inputs[index] - ch.offset) / ch.lsb);
	raw = CLAMP(raw, -(1LL << (cfg->registers[ch.reg].bytes * 8U - ch.shift - 1U)),
		    (1LL << (cfg->registers[ch.reg].bytes * 8U - ch.shift - 1U)) - 1);
	value = (uint32_t)raw << ch.shift;
	if (cfg->sample == NULL || cfg->sample(target, ch.reg, value)) {
		data->values[ch.reg] = value;
	}
}

void emul_regmap_convert(const struct emul *target)
{
	const struct emul_regmap_config *cfg = target->cfg;

	for (size_t i = 0; i < cfg->channel_count; i++) {
		convert_channel(target, i);
	}
}

static int set_channel(const struct emul *target, struct sensor_chan_spec ch,
		       const q31_t *value, int8_t shift)
{
	const struct emul_regmap_config *cfg = target->cfg;
	struct emul_regmap_data *data = target->data;
	int ret = -ENOTSUP;

	if (value == NULL || shift < -31 || shift > 31) {
		return -EINVAL;
	}
	if (ch.chan_idx != 0U) {
		return -ENOTSUP;
	}
	k_mutex_lock(&data->lock, K_FOREVER);
	for (size_t i = 0; i < cfg->channel_count; i++) {
		struct emul_regmap_channel spec = cfg->channels[i];
		double input = ldexp((double)*value, shift - 31);

		if (spec.channel != ch.chan_type) {
			continue;
		}
		if (cfg->channel != NULL) {
			cfg->channel(target, &spec);
		}
		if (input < spec.min || input > spec.max) {
			ret = -ERANGE;
			break;
		}
		data->inputs[i] = input;
		data->valid[i] = true;
		convert_channel(target, i);
		ret = 0;
		break;
	}
	k_mutex_unlock(&data->lock);
	return ret;
}

static int get_sample_range(const struct emul *target, struct sensor_chan_spec ch,
			    q31_t *lower, q31_t *upper, q31_t *epsilon, int8_t *shift)
{
	const struct emul_regmap_config *cfg = target->cfg;
	struct emul_regmap_data *data = target->data;
	int ret = -ENOTSUP;

	if (lower == NULL || upper == NULL || epsilon == NULL || shift == NULL) {
		return -EINVAL;
	}
	if (ch.chan_idx != 0U) {
		return -ENOTSUP;
	}
	k_mutex_lock(&data->lock, K_FOREVER);
	for (size_t i = 0; i < cfg->channel_count; i++) {
		struct emul_regmap_channel spec = cfg->channels[i];
		int8_t exponent = 0;

		if (spec.channel != ch.chan_type) {
			continue;
		}
		if (cfg->channel != NULL) {
			cfg->channel(target, &spec);
		}
		while (ldexp(1.0, exponent) <= MAX(fabs(spec.min), fabs(spec.max))) {
			exponent++;
		}
		*shift = exponent;
		*lower = (q31_t)ldexp(spec.min, 31 - exponent);
		*upper = (q31_t)ldexp(spec.max, 31 - exponent);
		*epsilon = MAX(1, (q31_t)ldexp(spec.lsb, 31 - exponent));
		ret = 0;
		break;
	}
	k_mutex_unlock(&data->lock);
	return ret;
}

/* Resolve byte addresses inside a multi-byte register. */
static int locate(const struct emul_regmap_config *cfg, uint16_t address, uint8_t *offset)
{
	if (address > UINT8_MAX) {
		return -EIO;
	}
	for (int reg = MIN(address, cfg->register_count - 1U); reg >= 0; reg--) {
		uint8_t bytes = cfg->registers[reg].bytes;

		if (bytes != 0U) {
			if (reg == address || (cfg->byte_addressed && address - reg < bytes)) {
				*offset = address - reg;
				return reg;
			}
			break;
		}
	}
	return -EIO;
}

static int transfer(const struct emul *target, struct i2c_msg *msgs, int num_msgs, int addr)
{
	const struct emul_regmap_config *cfg = target->cfg;
	struct emul_regmap_data *data = target->data;
	bool writing = false;
	uint16_t address;
	uint8_t offset = 0;
	uint32_t before_write = 0;
	uint32_t pending_write = 0;
	int write_reg = -1;
	int ret = 0;

	ARG_UNUSED(addr);
	if (msgs == NULL || num_msgs < 1) {
		return -EINVAL;
	}
	k_mutex_lock(&data->lock, K_FOREVER);
	address = data->pointer;
	for (int m = 0; m < num_msgs; m++) {
		bool read = (msgs[m].flags & I2C_MSG_READ) != 0U;
		size_t start = 0;

		if (msgs[m].buf == NULL || msgs[m].len == 0U ||
		    (msgs[m].flags & I2C_MSG_ADDR_10_BITS) != 0U) {
			ret = -EINVAL;
			break;
		}
		if (!read && (!writing || (msgs[m].flags & I2C_MSG_RESTART) != 0U)) {
			data->pointer = msgs[m].buf[0];
			address = data->pointer;
			offset = 0;
			start = 1;
		}
		for (size_t b = start; b < msgs[m].len; b++) {
			uint8_t byte_offset = offset;
			int reg = locate(cfg, address, &byte_offset);
			const struct emul_regmap_register *desc;
			uint32_t old;
			uint32_t mask;
			uint8_t shift;

			if (reg < 0) {
				ret = reg;
				goto out;
			}
			desc = &cfg->registers[reg];
			if (!cfg->byte_addressed) {
				byte_offset = offset;
			}
			if (byte_offset >= desc->bytes) {
				ret = -EIO;
				goto out;
			}
			shift = 8U * (cfg->little_endian ? byte_offset :
				      desc->bytes - byte_offset - 1U);
			old = data->values[reg];
			if (byte_offset == 0U || reg != write_reg) {
				before_write = old;
				pending_write = old;
				write_reg = reg;
			}
			mask = 0xffU << shift;
			if (read) {
				msgs[m].buf[b] = old >> shift;
				data->values[reg] &= ~(desc->clear_on_read & mask);
				if (cfg->read != NULL) {
					cfg->read(target, address);
				}
			} else {
				mask &= desc->write_mask;
				pending_write = (pending_write & ~mask) |
					(((uint32_t)msgs[m].buf[b] << shift) & mask);
				if (cfg->byte_addressed || byte_offset + 1U == desc->bytes) {
					data->values[reg] = pending_write;
				}
				if (cfg->write != NULL && byte_offset + 1U == desc->bytes) {
					cfg->write(target, reg, before_write);
				}
			}
			if (cfg->byte_addressed) {
				if (cfg->increment_mask == 0U ||
				    (data->values[cfg->increment_reg] &
				     cfg->increment_mask) != 0U) {
					address++;
				}
			} else {
				offset++;
			}
		}
		writing = !read && (msgs[m].flags & I2C_MSG_STOP) == 0U;
		if ((msgs[m].flags & I2C_MSG_STOP) != 0U) {
			address = data->pointer;
			offset = 0;
		}
	}
out:
	k_mutex_unlock(&data->lock);
	return ret;
}

int emul_regmap_init(const struct emul *target, const struct device *parent)
{
	const struct emul_regmap_config *cfg = target->cfg;
	struct emul_regmap_data *data = target->data;

	ARG_UNUSED(parent);
	if (cfg->registers == NULL || cfg->register_count == 0U ||
	    cfg->register_count > 256U) {
		return -EINVAL;
	}
	for (size_t i = 0; i < cfg->register_count; i++) {
		if (cfg->registers[i].bytes > 4U) {
			return -EINVAL;
		}
	}
	k_mutex_init(&data->lock);
	emul_regmap_reset(target);
	return 0;
}

const struct i2c_emul_api emul_regmap_i2c_api = {.transfer = transfer};
const struct emul_sensor_driver_api emul_regmap_sensor_api = {
	.set_channel = set_channel,
	.get_sample_range = get_sample_range,
};
