/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_lpc8n04_syscon

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/dt-bindings/clock/lpc8n04-clock.h>
#include <zephyr/sys/util.h>

#include <soc.h>

#define LPC8N04_SFRO_HZ 8000000U

#define LPC8N04_AHB_CLK_MASK                                                                       \
	(BIT(LPC8N04_CLK_RAM) | BIT(LPC8N04_CLK_FLASH_REG) | BIT(LPC8N04_CLK_FLASH) |              \
	 GENMASK(LPC8N04_CLK_TSENS, LPC8N04_CLK_I2C0) | BIT(LPC8N04_CLK_IOCON) |                   \
	 BIT(LPC8N04_CLK_EEPROM_REG) | BIT(LPC8N04_CLK_EEPROM))

struct clock_control_lpc8n04_config {
	SYSCON_Type *base;
};

static int clock_control_lpc8n04_bit(clock_control_subsys_t sub_system, uint32_t *bit)
{
	uint32_t name = (uint32_t)(uintptr_t)sub_system;

	if (name > 31U || (BIT(name) & LPC8N04_AHB_CLK_MASK) == 0U) {
		return -EINVAL;
	}

	*bit = BIT(name);

	return 0;
}

static int clock_control_lpc8n04_on(const struct device *dev, clock_control_subsys_t sub_system)
{
	const struct clock_control_lpc8n04_config *config = dev->config;
	uint32_t bit;
	int ret;

	ret = clock_control_lpc8n04_bit(sub_system, &bit);
	if (ret != 0) {
		return ret;
	}

	config->base->SYSAHBCLKCTRL |= bit;

	return 0;
}

static int clock_control_lpc8n04_off(const struct device *dev, clock_control_subsys_t sub_system)
{
	const struct clock_control_lpc8n04_config *config = dev->config;
	uint32_t bit;
	int ret;

	ret = clock_control_lpc8n04_bit(sub_system, &bit);
	if (ret != 0) {
		return ret;
	}

	config->base->SYSAHBCLKCTRL &= ~bit;

	return 0;
}

static enum clock_control_status clock_control_lpc8n04_get_status(const struct device *dev,
								  clock_control_subsys_t sub_system)
{
	const struct clock_control_lpc8n04_config *config = dev->config;
	uint32_t bit;

	if (clock_control_lpc8n04_bit(sub_system, &bit) != 0) {
		return CLOCK_CONTROL_STATUS_UNKNOWN;
	}

	return (config->base->SYSAHBCLKCTRL & bit) != 0U ? CLOCK_CONTROL_STATUS_ON
							 : CLOCK_CONTROL_STATUS_OFF;
}

static int clock_control_lpc8n04_get_rate(const struct device *dev,
					  clock_control_subsys_t sub_system, uint32_t *rate)
{
	const struct clock_control_lpc8n04_config *config = dev->config;
	uint32_t bit;
	int ret;

	ret = clock_control_lpc8n04_bit(sub_system, &bit);
	if (ret != 0) {
		return ret;
	}

	*rate = LPC8N04_SFRO_HZ >>
		FIELD_GET(SYSCON_SYSCLKCTRL_SYSCLKDIV_MASK, config->base->SYSCLKCTRL);

	return 0;
}

static DEVICE_API(clock_control, clock_control_lpc8n04_api) = {
	.on = clock_control_lpc8n04_on,
	.off = clock_control_lpc8n04_off,
	.get_status = clock_control_lpc8n04_get_status,
	.get_rate = clock_control_lpc8n04_get_rate,
};

static const struct clock_control_lpc8n04_config clock_control_lpc8n04_config = {
	.base = (SYSCON_Type *)DT_INST_REG_ADDR(0),
};

DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, &clock_control_lpc8n04_config, PRE_KERNEL_1,
		      CONFIG_CLOCK_CONTROL_INIT_PRIORITY, &clock_control_lpc8n04_api);
