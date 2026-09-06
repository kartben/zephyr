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

/* Shift of the byte at bus position pos within an n byte register. */
static unsigned int byte_shift(const struct emul_sensor_regmap *desc, uint8_t n, uint8_t pos)
{
	return 8U * (desc->big_endian ? (n - 1U - pos) : pos);
}

static void advance(struct emul_sensor_regmap_data *data, uint8_t n)
{
	data->pos++;
	if (data->pos >= n) {
		data->pos = 0;
		data->ptr++;
	}
}

static int read_byte(const struct emul *target, uint8_t *out)
{
	const struct emul_sensor_regmap *desc = target->cfg;
	struct emul_sensor_regmap_data *data = target->data;
	const struct emul_sensor_reg *reg = find_reg(desc, data->ptr);
	uint8_t n;

	if (reg == NULL) {
		LOG_WRN("%s: read of unknown register 0x%02x", target->dev->name, data->ptr);
		return -EIO;
	}

	n = reg_bytes(desc, reg);
	if (data->pos == 0U) {
		LOG_DBG("%s: rd %s (0x%02x) = 0x%0*x", target->dev->name, reg->name, data->ptr,
			2 * n, data->regs[data->ptr]);
	}
	*out = (data->regs[data->ptr] >> byte_shift(desc, n, data->pos)) & 0xFFU;
	if (data->pos == n - 1U) {
		data->regs[data->ptr] &= ~reg->clear_on_read;
	}
	advance(data, n);

	return 0;
}

static int write_byte(const struct emul *target, uint8_t val)
{
	const struct emul_sensor_regmap *desc = target->cfg;
	struct emul_sensor_regmap_data *data = target->data;
	const struct emul_sensor_reg *reg = find_reg(desc, data->ptr);
	uint8_t n;

	if (reg == NULL) {
		LOG_WRN("%s: write of unknown register 0x%02x", target->dev->name, data->ptr);
		return -EIO;
	}

	n = reg_bytes(desc, reg);
	if ((reg->flags & EMUL_SENSOR_REG_RO) != 0U) {
		LOG_DBG("%s: write to read-only %s (0x%02x) ignored", target->dev->name, reg->name,
			data->ptr);
	} else {
		unsigned int shift = byte_shift(desc, n, data->pos);
		uint32_t mask = 0xFFU << shift;
		uint32_t *r = &data->regs[data->ptr];

		if (reg->write_mask != 0U) {
			mask &= reg->write_mask;
		}
		*r = (*r & ~mask) | (((uint32_t)val << shift) & mask);
		*r &= ~reg->self_clear;
		if (data->pos == n - 1U) {
			LOG_DBG("%s: wr %s (0x%02x) = 0x%0*x", target->dev->name, reg->name,
				data->ptr, 2 * n, *r);
		}
	}
	advance(data, n);

	return 0;
}

static int regmap_i2c_transfer(const struct emul *target, struct i2c_msg *msgs, int num_msgs,
			       int addr)
{
	const struct emul_sensor_regmap *desc = target->cfg;
	struct emul_sensor_regmap_data *data = target->data;
	int ret;

	ARG_UNUSED(addr);

	data->pos = 0;

	for (int i = 0; i < num_msgs; i++) {
		struct i2c_msg *msg = &msgs[i];
		uint32_t j = 0;

		if ((msg->flags & I2C_MSG_READ) != 0U) {
			for (; j < msg->len; j++) {
				ret = read_byte(target, &msg->buf[j]);
				if (ret != 0) {
					return ret;
				}
			}
			continue;
		}

		if ((i == 0 || (msg->flags & I2C_MSG_RESTART) != 0U) && msg->len > 0U) {
			data->ptr = msg->buf[0] & ~desc->addr_ignore;
			data->pos = 0;
			j = 1;
		}
		for (; j < msg->len; j++) {
			ret = write_byte(target, msg->buf[j]);
			if (ret != 0) {
				return ret;
			}
		}
	}

	return 0;
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

static int regmap_set_channel(const struct emul *target, struct sensor_chan_spec ch,
			      const q31_t *value, int8_t shift)
{
	const struct emul_sensor_regmap *desc = target->cfg;
	struct emul_sensor_regmap_data *data = target->data;
	const struct emul_sensor_channel *c = find_channel(desc, ch);
	struct emul_sensor_field f;
	uint8_t bytes, nregs;
	double raw_d;
	int64_t raw, lo, hi;
	uint64_t word, mask;

	if (c == NULL) {
		return -ENOTSUP;
	}

	f = active_field(target, c);
	raw_d = ((double)*value * pow2(shift) / Q31_ONE - c->offset) / f.lsb;
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
	word = word_get(target, c, nregs, bytes);
	word = (word & ~mask) | (((uint64_t)raw << f.pos) & mask);
	word_set(target, c, nregs, bytes, word);

	if (c->ready.mask != 0U) {
		data->regs[c->ready.reg] |= c->ready.mask;
	}

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

	if (c == NULL) {
		return -ENOTSUP;
	}

	f = active_field(target, c);
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
	*epsilon = to_q31(f.lsb, s);

	return 0;
}

int emul_sensor_regmap_init(const struct emul *target, const struct device *parent)
{
	const struct emul_sensor_regmap *desc = target->cfg;
	struct emul_sensor_regmap_data *data = target->data;

	ARG_UNUSED(parent);

	memset(data, 0, sizeof(*data));
	for (size_t i = 0; i < desc->num_regs; i++) {
		data->regs[desc->regs[i].addr] = desc->regs[i].reset;
	}

	return 0;
}

uint32_t emul_sensor_regmap_get_reg(const struct emul *target, uint8_t addr)
{
	struct emul_sensor_regmap_data *data = target->data;

	return data->regs[addr];
}

void emul_sensor_regmap_set_reg(const struct emul *target, uint8_t addr, uint32_t val)
{
	struct emul_sensor_regmap_data *data = target->data;

	data->regs[addr] = val;
}

const struct i2c_emul_api emul_sensor_regmap_i2c_api = {
	.transfer = regmap_i2c_transfer,
};

const struct emul_sensor_driver_api emul_sensor_regmap_backend_api = {
	.set_channel = regmap_set_channel,
	.get_sample_range = regmap_get_sample_range,
};
