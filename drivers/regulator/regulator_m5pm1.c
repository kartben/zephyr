/*
 * Copyright (c) 2026 Benjamin Cabé <benjamin@zephyrproject.org>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT m5stack_m5pm1_regulator

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/mfd/m5pm1.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

enum m5pm1_rail {
	M5PM1_RAIL_DCDC,
	M5PM1_RAIL_LDO,
	M5PM1_RAIL_BOOST,
};

struct regulator_m5pm1_desc {
	uint8_t enable_mask;
	int32_t voltage_uv;
};

static const struct regulator_m5pm1_desc m5pm1_dcdc_desc = {
	.enable_mask = M5PM1_PWR_CFG_DCDC_EN,
	.voltage_uv = 5000000,
};

static const struct regulator_m5pm1_desc m5pm1_ldo_desc = {
	.enable_mask = M5PM1_PWR_CFG_LDO_EN,
	.voltage_uv = 3300000,
};

static const struct regulator_m5pm1_desc m5pm1_boost_desc = {
	.enable_mask = M5PM1_PWR_CFG_BOOST_EN,
	.voltage_uv = 5000000,
};

struct regulator_m5pm1_config {
	struct regulator_common_config common;
	const struct device *mfd;
	const struct regulator_m5pm1_desc *desc;
};

struct regulator_m5pm1_data {
	struct regulator_common_data data;
};

static int regulator_m5pm1_enable(const struct device *dev)
{
	const struct regulator_m5pm1_config *config = dev->config;

	return mfd_m5pm1_update_reg(config->mfd, M5PM1_REG_PWR_CFG, config->desc->enable_mask,
				    config->desc->enable_mask);
}

static int regulator_m5pm1_disable(const struct device *dev)
{
	const struct regulator_m5pm1_config *config = dev->config;

	return mfd_m5pm1_update_reg(config->mfd, M5PM1_REG_PWR_CFG, config->desc->enable_mask, 0);
}

static int regulator_m5pm1_get_voltage(const struct device *dev, int32_t *volt_uv)
{
	const struct regulator_m5pm1_config *config = dev->config;

	*volt_uv = config->desc->voltage_uv;
	return 0;
}

static unsigned int regulator_m5pm1_count_voltages(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 1U;
}

static int regulator_m5pm1_list_voltage(const struct device *dev, unsigned int idx,
					int32_t *volt_uv)
{
	const struct regulator_m5pm1_config *config = dev->config;

	if (idx != 0U) {
		return -EINVAL;
	}

	*volt_uv = config->desc->voltage_uv;
	return 0;
}

static int regulator_m5pm1_init(const struct device *dev)
{
	const struct regulator_m5pm1_config *config = dev->config;

	if (!device_is_ready(config->mfd)) {
		return -ENODEV;
	}

	regulator_common_data_init(dev);

	return regulator_common_init(dev, false);
}

static DEVICE_API(regulator, regulator_m5pm1_api) = {
	.enable = regulator_m5pm1_enable,
	.disable = regulator_m5pm1_disable,
	.count_voltages = regulator_m5pm1_count_voltages,
	.list_voltage = regulator_m5pm1_list_voltage,
	.get_voltage = regulator_m5pm1_get_voltage,
};

#define REGULATOR_M5PM1_DEFINE(node_id, id, _desc)                                                 \
	static const struct regulator_m5pm1_config regulator_m5pm1_config_##id = {                 \
		.common = REGULATOR_DT_COMMON_CONFIG_INIT(node_id),                                \
		.mfd = DEVICE_DT_GET(DT_GPARENT(node_id)),                                         \
		.desc = &_desc,                                                                    \
	};                                                                                         \
                                                                                                   \
	static struct regulator_m5pm1_data regulator_m5pm1_data_##id;                              \
                                                                                                   \
	DEVICE_DT_DEFINE(node_id, regulator_m5pm1_init, NULL, &regulator_m5pm1_data_##id,          \
			 &regulator_m5pm1_config_##id, POST_KERNEL,                                \
			 CONFIG_REGULATOR_M5PM1_INIT_PRIORITY, &regulator_m5pm1_api);

#define REGULATOR_M5PM1_DEFINE_COND(inst, child, desc)                                             \
	COND_CODE_1(DT_NODE_EXISTS(DT_INST_CHILD(inst, child)),                                    \
		    (REGULATOR_M5PM1_DEFINE(DT_INST_CHILD(inst, child), child##inst, desc)), ())

#define REGULATOR_M5PM1_DEFINE_ALL(inst)                                                           \
	REGULATOR_M5PM1_DEFINE_COND(inst, dcdc, m5pm1_dcdc_desc)                                   \
	REGULATOR_M5PM1_DEFINE_COND(inst, ldo, m5pm1_ldo_desc)                                     \
	REGULATOR_M5PM1_DEFINE_COND(inst, boost, m5pm1_boost_desc)

DT_INST_FOREACH_STATUS_OKAY(REGULATOR_M5PM1_DEFINE_ALL)
