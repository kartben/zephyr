/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/emul_sensor.h>
#include <zephyr/drivers/emul_sensor_regmap.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(emul_sensor_regmap, CONFIG_SENSOR_LOG_LEVEL);

#define Q31_ONE 2147483648.0

static const struct emul_sensor_reg *find_reg(const struct emul_sensor_regmap *desc, uint8_t addr)
{
	for (size_t i = 0; i < desc->num_regs; i++) {
		if (desc->regs[i].addr == addr) {
			return &desc->regs[i];
		}
	}

	return NULL;
}

static uint8_t reg_bytes(const struct emul_sensor_regmap *desc, const struct emul_sensor_reg *reg)
{
	if (reg != NULL && reg->bytes != 0U) {
		return reg->bytes;
	}

	return MAX(desc->reg_bytes, 1U);
}

static bool field_matches(const struct emul_sensor_regmap_data *data,
			  struct emul_sensor_condition field)
{
	return field.mask != 0U && (data->regs[field.reg] & field.mask) == field.value;
}

static int locate(const struct emul_sensor_regmap *desc, uint16_t address, uint8_t *offset)
{
	if (address > UINT8_MAX) {
		return -EIO;
	}
	for (size_t i = 0; i < desc->num_regs; i++) {
		const struct emul_sensor_reg *r = &desc->regs[i];

		if (address == r->addr || (desc->byte_addressed && address > r->addr &&
		    address - r->addr < reg_bytes(desc, r))) {
			*offset = address - r->addr;
			return r->addr;
		}
	}
	return -EIO;
}

static void register_written(const struct emul *target, uint8_t reg, uint32_t old)
{
	const struct emul_sensor_regmap *cfg = target->cfg;
	struct emul_sensor_regmap_data *data = target->data;
	const struct emul_sensor_reg *desc = find_reg(cfg, reg);
	uint32_t command = data->regs[reg];

	if ((command & desc->reset_on_write) != 0U) {
		emul_sensor_regmap_reset(target);
		return;
	}
	if (cfg->write != NULL) {
		cfg->write(target, reg, old);
	}
	if ((data->regs[reg] & desc->convert_on_write) != 0U) {
		bool was_disabled = reg == cfg->disabled.reg ?
			(old & cfg->disabled.mask) == cfg->disabled.value :
			field_matches(data, cfg->disabled);

		if (!desc->requires_standby || was_disabled) {
			data->force_conversion = field_matches(data, cfg->disabled);
			emul_sensor_regmap_convert(target);
			data->force_conversion = false;
		}
	}
	data->regs[reg] &= ~desc->self_clear;
}

static int transfer(const struct emul *target, struct i2c_msg *msgs, int num_msgs, int addr)
{
	const struct emul_sensor_regmap *cfg = target->cfg;
	struct emul_sensor_regmap_data *data = target->data;
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
	address = data->ptr;
	offset = data->pos;
	for (int m = 0; m < num_msgs; m++) {
		bool read = (msgs[m].flags & I2C_MSG_READ) != 0U;
		size_t start = 0;

		if (msgs[m].buf == NULL || msgs[m].len == 0U ||
		    (msgs[m].flags & I2C_MSG_ADDR_10_BITS) != 0U) {
			ret = -EINVAL;
			break;
		}
		if (!read && (!writing || (msgs[m].flags & I2C_MSG_RESTART) != 0U)) {
			data->ptr = msgs[m].buf[0] & ~cfg->addr_ignore;
			address = data->ptr;
			offset = 0;
			start = 1;
		}
		for (size_t b = start; b < msgs[m].len; b++) {
			uint8_t byte_offset = offset;
			int reg = locate(cfg, address, &byte_offset);
			const struct emul_sensor_reg *desc;
			uint32_t old;
			uint32_t mask;
			uint8_t shift;

			if (reg < 0) {
				ret = reg;
				goto out;
			}
			desc = find_reg(cfg, reg);
			if (!cfg->byte_addressed) {
				byte_offset = offset;
			}
			if (byte_offset >= reg_bytes(cfg, desc)) {
				ret = -EIO;
				goto out;
			}
			shift = 8U * (!cfg->big_endian ? byte_offset :
				      reg_bytes(cfg, desc) - byte_offset - 1U);
			old = data->regs[reg];
			LOG_DBG("%s %s (0x%02x) byte %u", read ? "read" : "write",
				desc->name, reg, byte_offset);
			if (byte_offset == 0U || reg != write_reg) {
				before_write = old;
				pending_write = old;
				write_reg = reg;
			}
			mask = 0xffU << shift;
			if (read) {
				msgs[m].buf[b] = old >> shift;
				if (byte_offset + 1U == reg_bytes(cfg, desc)) {
					data->regs[reg] &= ~desc->clear_on_read;
				}
				if (byte_offset + 1U == reg_bytes(cfg, desc) &&
				    desc->read_clears.mask != 0U) {
					struct emul_sensor_condition clear = desc->read_clears;

					data->regs[clear.reg] &= ~clear.mask;
				}
				if (cfg->read != NULL) {
					cfg->read(target, address);
				}
			} else {
				if ((desc->flags & EMUL_SENSOR_REG_RO) != 0U) {
					mask = 0;
				} else if (desc->write_mask != 0U) {
					mask &= desc->write_mask;
				}
				pending_write = (pending_write & ~mask) |
					(((uint32_t)msgs[m].buf[b] << shift) & mask);
				if (cfg->byte_addressed ||
				    byte_offset + 1U == reg_bytes(cfg, desc)) {
					data->regs[reg] = pending_write;
				}
				if (byte_offset + 1U == reg_bytes(cfg, desc)) {
					register_written(target, reg, before_write);
				}
			}
			if (cfg->byte_addressed) {
				if (cfg->increment.mask == 0U ||
				    (data->regs[cfg->increment.reg] &
				     cfg->increment.mask) != 0U) {
					address++;
				}
			} else {
				offset++;
				if (!cfg->fixed_pointer && offset == reg_bytes(cfg, desc)) {
					offset = 0;
					address++;
				}
			}
		}
		writing = !read && (msgs[m].flags & I2C_MSG_STOP) == 0U;
		if (cfg->fixed_pointer && (msgs[m].flags & I2C_MSG_STOP) != 0U) {
			address = data->ptr;
			offset = 0;
		}
	}
out:
	if (!cfg->fixed_pointer) {
		data->ptr = address;
		data->pos = offset;
	}
	k_mutex_unlock(&data->lock);
	return ret;
}

static const struct emul_sensor_channel *find_channel(const struct emul_sensor_regmap *desc,
						      struct sensor_chan_spec ch)
{
	if (ch.chan_idx != 0U) {
		return NULL;
	}

	for (size_t i = 0; i < desc->num_channels; i++) {
		if (desc->channels[i].chan == ch.chan_type) {
			return &desc->channels[i];
		}
	}

	return NULL;
}

static struct emul_sensor_field active_field(const struct emul *target,
					     const struct emul_sensor_channel *c)
{
	struct emul_sensor_regmap_data *data = target->data;
	struct emul_sensor_field f = {
		.bits = c->bits, .pos = c->pos, .lsb = c->lsb, .min = c->min, .max = c->max};

	if (c->select.mask != 0U) {
		uint32_t sel = FIELD_GET(c->select.mask, data->regs[c->select.reg]);
		const struct emul_sensor_field *v =
			&c->variants[MIN(sel, ARRAY_SIZE(c->variants) - 1)];

		if (v->bits != 0U) {
			f.bits = v->bits;
		}
		if (v->pos != 0U) {
			f.pos = v->pos;
		}
		if (v->lsb != 0.0) {
			f.lsb = v->lsb;
		}
		if (v->min != 0.0 || v->max != 0.0) {
			f.min = v->min;
			f.max = v->max;
		}
	}

	return f;
}

/* Data word made of the registers holding the field, in the byte order of the device. */
static uint64_t word_get(const struct emul *target, const struct emul_sensor_channel *c,
			 uint8_t nregs, uint8_t bytes)
{
	const struct emul_sensor_regmap *desc = target->cfg;
	struct emul_sensor_regmap_data *data = target->data;
	uint64_t word = 0;

	for (uint8_t k = 0; k < nregs; k++) {
		uint64_t v = data->regs[(uint8_t)(c->reg + k)];

		if (desc->big_endian) {
			word = (word << (8U * bytes)) | v;
		} else {
			word |= v << (8U * bytes * k);
		}
	}

	return word;
}

static void word_set(const struct emul *target, const struct emul_sensor_channel *c, uint8_t nregs,
		     uint8_t bytes, uint64_t word)
{
	const struct emul_sensor_regmap *desc = target->cfg;
	struct emul_sensor_regmap_data *data = target->data;
	uint64_t mask = BIT64_MASK(8U * bytes);

	for (uint8_t k = 0; k < nregs; k++) {
		unsigned int shift = 8U * bytes * (desc->big_endian ? (nregs - 1U - k) : k);

		data->regs[(uint8_t)(c->reg + k)] = (word >> shift) & mask;
	}
}

static double pow2(int e)
{
	double r = 1.0;

	for (; e > 0; e--) {
		r *= 2.0;
	}
	for (; e < 0; e++) {
		r /= 2.0;
	}

	return r;
}

static q31_t to_q31(double v, int8_t shift)
{
	return (q31_t)(v * Q31_ONE / pow2(shift));
}

static void convert_channel(const struct emul *target, size_t index)
{
	const struct emul_sensor_regmap *desc = target->cfg;
	struct emul_sensor_regmap_data *data = target->data;
	const struct emul_sensor_channel *c = &desc->channels[index];
	struct emul_sensor_field f;
	uint8_t bytes, nregs;
	double raw_d;
	int64_t raw, lo, hi;
	uint64_t word, mask;

	if (!data->valid[index] ||
	    (!data->force_conversion && field_matches(data, desc->disabled)) ||
	    field_matches(data, c->disabled)) {
		return;
	}
	if (c->ready.mask != 0U && (data->regs[c->ready.reg] & c->ready.mask) != 0U) {
		if (field_matches(data, desc->block_update)) {
			return;
		}
		data->regs[c->ready.reg] |= c->overrun;
	}
	f = active_field(target, c);
	if (f.bits == 0U || f.bits > 32U || f.pos + f.bits > 64U || f.lsb <= 0.0) {
		return;
	}
	raw_d = (data->inputs[index] - c->offset) / f.lsb;
	raw = (int64_t)(raw_d + (raw_d >= 0.0 ? 0.5 : -0.5));
	if (c->is_signed) {
		lo = -(1LL << (f.bits - 1U));
		hi = (1LL << (f.bits - 1U)) - 1;
	} else {
		lo = 0;
		hi = (1LL << f.bits) - 1;
	}
	raw = CLAMP(raw, lo, hi);

	bytes = reg_bytes(desc, find_reg(desc, c->reg));
	nregs = DIV_ROUND_UP(f.pos + f.bits, 8U * bytes);
	mask = BIT64_MASK(f.bits) << f.pos;
	word = c->whole_word ? 0 : word_get(target, c, nregs, bytes);
	word = (word & ~mask) | (((uint64_t)raw << f.pos) & mask);
	if (desc->sample != NULL && !desc->sample(target, c->reg, (uint32_t)word)) {
		return;
	}
	word_set(target, c, nregs, bytes, word);

	if (c->ready.mask != 0U) {
		data->regs[c->ready.reg] |= c->ready.mask;
	}
}

void emul_sensor_regmap_convert(const struct emul *target)
{
	const struct emul_sensor_regmap *desc = target->cfg;

	for (size_t i = 0; i < desc->num_channels; i++) {
		convert_channel(target, i);
	}
}

static int regmap_set_channel(const struct emul *target, struct sensor_chan_spec ch,
			      const q31_t *value, int8_t shift)
{
	const struct emul_sensor_regmap *desc = target->cfg;
	struct emul_sensor_regmap_data *data = target->data;
	const struct emul_sensor_channel *c = find_channel(desc, ch);

	if (value == NULL || shift < -31 || shift > 31) {
		return -EINVAL;
	}
	if (c == NULL) {
		return -ENOTSUP;
	}
	k_mutex_lock(&data->lock, K_FOREVER);
	double input = (double)*value * pow2(shift) / Q31_ONE;
	struct emul_sensor_field f = active_field(target, c);

	if (f.bits == 0U || f.bits > 32U || f.pos + f.bits > 64U || f.lsb <= 0.0) {
		k_mutex_unlock(&data->lock);
		return -ENOTSUP;
	}
	if ((f.min != 0.0 || f.max != 0.0) &&
	    (input < f.min - f.lsb || input > f.max + f.lsb)) {
		k_mutex_unlock(&data->lock);
		return -ERANGE;
	}
	data->inputs[c - desc->channels] = input;
	data->valid[c - desc->channels] = true;
	convert_channel(target, c - desc->channels);
	k_mutex_unlock(&data->lock);
	return 0;
}

static int regmap_get_sample_range(const struct emul *target, struct sensor_chan_spec ch,
				   q31_t *lower, q31_t *upper, q31_t *epsilon, int8_t *shift)
{
	const struct emul_sensor_regmap *desc = target->cfg;
	const struct emul_sensor_channel *c = find_channel(desc, ch);
	struct emul_sensor_field f;
	double lo, hi, absmax;
	int8_t s = 0;

	if (lower == NULL || upper == NULL || epsilon == NULL || shift == NULL) {
		return -EINVAL;
	}
	if (c == NULL) {
		return -ENOTSUP;
	}

	struct emul_sensor_regmap_data *data = target->data;

	k_mutex_lock(&data->lock, K_FOREVER);
	f = active_field(target, c);
	if (f.bits == 0U || f.bits > 32U || f.pos + f.bits > 64U || f.lsb <= 0.0) {
		k_mutex_unlock(&data->lock);
		return -ENOTSUP;
	}
	lo = f.min;
	hi = f.max;
	if (lo == 0.0 && hi == 0.0) {
		if (c->is_signed) {
			lo = c->offset - pow2(f.bits - 1) * f.lsb;
			hi = c->offset + (pow2(f.bits - 1) - 1.0) * f.lsb;
		} else {
			lo = c->offset;
			hi = c->offset + (pow2(f.bits) - 1.0) * f.lsb;
		}
	}

	absmax = MAX(lo < 0.0 ? -lo : lo, hi < 0.0 ? -hi : hi);
	while (absmax >= pow2(s) && s < 31) {
		s++;
	}

	*shift = s;
	*lower = to_q31(lo, s);
	*upper = to_q31(hi, s);
	*epsilon = MAX(1, to_q31(f.lsb, s));
	k_mutex_unlock(&data->lock);

	return 0;
}

int emul_sensor_regmap_init(const struct emul *target, const struct device *parent)
{
	const struct emul_sensor_regmap *desc = target->cfg;
	struct emul_sensor_regmap_data *data = target->data;

	ARG_UNUSED(parent);

	if (desc->regs == NULL || desc->num_regs == 0U || desc->num_regs > 256U) {
		return -EINVAL;
	}
	for (size_t i = 0; i < desc->num_regs; i++) {
		if (reg_bytes(desc, &desc->regs[i]) > 4U) {
			return -EINVAL;
		}
	}
	if (desc->num_channels > 0U &&
	    (desc->channels == NULL || data->inputs == NULL || data->valid == NULL)) {
		return -EINVAL;
	}
	k_mutex_init(&data->lock);
	if (desc->num_channels > 0U) {
		memset(data->valid, 0, desc->num_channels * sizeof(*data->valid));
	}
	emul_sensor_regmap_reset(target);
	return 0;
}

void emul_sensor_regmap_reset(const struct emul *target)
{
	const struct emul_sensor_regmap *desc = target->cfg;
	struct emul_sensor_regmap_data *data = target->data;

	k_mutex_lock(&data->lock, K_FOREVER);
	memset(data->regs, 0, sizeof(data->regs));
	data->ptr = 0;
	data->pos = 0;
	for (size_t i = 0; i < desc->num_regs; i++) {
		data->regs[desc->regs[i].addr] = desc->regs[i].reset;
	}
	k_mutex_unlock(&data->lock);
}

uint32_t emul_sensor_regmap_get_reg(const struct emul *target, uint8_t addr)
{
	struct emul_sensor_regmap_data *data = target->data;

	uint32_t value;

	k_mutex_lock(&data->lock, K_FOREVER);
	value = data->regs[addr];
	k_mutex_unlock(&data->lock);
	return value;
}

void emul_sensor_regmap_set_reg(const struct emul *target, uint8_t addr, uint32_t val)
{
	struct emul_sensor_regmap_data *data = target->data;

	k_mutex_lock(&data->lock, K_FOREVER);
	data->regs[addr] = val;
	k_mutex_unlock(&data->lock);
}

const struct i2c_emul_api emul_sensor_regmap_i2c_api = {
	.transfer = transfer,
};

const struct emul_sensor_driver_api emul_sensor_regmap_backend_api = {
	.set_channel = regmap_set_channel,
	.get_sample_range = regmap_get_sample_range,
};
