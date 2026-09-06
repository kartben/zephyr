/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT microchip_tcn75a

#include "regmap.h"

/* DS21935D, sections 5.3-5.4: https://ww1.microchip.com/downloads/en/DeviceDoc/21935D.pdf */
enum { TA = 0x00, CONFIG = 0x01, THYST = 0x02, TSET = 0x03 };

static const struct emul_regmap_register registers[] = {
	[TA] = {.bytes = 2},
	[CONFIG] = {.bytes = 1, .write_mask = 0xff},
	/* Register diagrams 5-4/5-5 specify 75/80 C; Table 5-4 hex values disagree. */
	[THYST] = {.bytes = 2, .reset = 0x4b00, .write_mask = 0xff80},
	[TSET] = {.bytes = 2, .reset = 0x5000, .write_mask = 0xff80},
};

static const struct emul_regmap_channel channels[] = {
	{.channel = SENSOR_CHAN_AMBIENT_TEMP, .reg = TA, .lsb = 0.5,
	 .min = -40, .max = 125, .shift = 7},
};

static void channel(const struct emul *target, struct emul_regmap_channel *ch)
{
	struct emul_regmap_data *data = target->data;
	uint8_t resolution = (data->values[CONFIG] >> 5) & 3U;

	ch->lsb = 0.5 / (1U << resolution);
	ch->shift = 7U - resolution;
}

static bool sample(const struct emul *target, uint8_t reg, uint32_t value)
{
	struct emul_regmap_data *data = target->data;

	ARG_UNUSED(reg);
	ARG_UNUSED(value);
	return (data->values[CONFIG] & BIT(0)) == 0U;
}

static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_regmap_data *data = target->data;
	uint32_t conf = data->values[CONFIG];

	if (reg == CONFIG) {
		if ((old & 0x81U) == 1U && (conf & 0x81U) == 0x81U) {
			data->values[CONFIG] &= ~BIT(0);
		}
		emul_regmap_convert(target);
		data->values[CONFIG] = conf & ~BIT(7);
	}
}

static const struct emul_regmap_config config = {
	.registers = registers, .register_count = ARRAY_SIZE(registers),
	.channels = channels, .channel_count = ARRAY_SIZE(channels),
	.write = write, .channel = channel, .sample = sample,
};

#define DEFINE(inst) EMUL_REGMAP_DT_INST_DEFINE(inst, config, registers, channels);
DT_INST_FOREACH_STATUS_OKAY(DEFINE)
