/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_enet_qos_mdio

#include <zephyr/net/mdio.h>
#include <zephyr/drivers/mdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/ethernet/eth_nxp_enet_qos.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mdio_nxp_enet_qos, CONFIG_MDIO_LOG_LEVEL);

struct nxp_enet_qos_mdio_config {
	const struct device *enet_dev;
};

struct nxp_enet_qos_mdio_data {
	struct k_mutex mdio_mutex;
#if IS_ENABLED(CONFIG_MDIO_NXP_ENET_QOS_RUNTIME_CR_UPDATE)
	uint32_t last_mdio_clk_hz;
#endif
};

struct mdio_transaction {
	enum mdio_opcode op;
	union {
		uint16_t write_data;
		uint16_t *read_data;
	};
	uint8_t portaddr;
	uint8_t regaddr;
	uint16_t regaddr_c45;
	enet_qos_t *base;
	struct k_mutex *mdio_bus_mutex;
	const struct device *mdio_dev;
};

static int mdio_nxp_enet_qos_divider_from_clk_hz(uint32_t clk_hz, int *div_out)
{
	uint32_t mhz = clk_hz / 1000000U;

	if (mhz >= 20U && mhz < 35U) {
		*div_out = 2;
	} else if (mhz < 60U) {
		*div_out = 3;
	} else if (mhz < 100U) {
		*div_out = 0;
	} else if (mhz < 150U) {
		*div_out = 1;
	} else if (mhz < 250U) {
		*div_out = 4;
	} else {
		return -ENOTSUP;
	}

	return 0;
}

static int mdio_nxp_enet_qos_apply_cr(enet_qos_t *base, uint32_t clk_rate_hz)
{
	int divider;
	int err = mdio_nxp_enet_qos_divider_from_clk_hz(clk_rate_hz, &divider);

	if (err != 0) {
		LOG_ERR("ENET QOS clk rate does not allow MDIO");
		return err;
	}

	uint32_t mdio_addr = base->MAC_MDIO_ADDRESS;

	mdio_addr &= ~_ENET_QOS_REG_MASK(MAC_MDIO_ADDRESS, CR);
	mdio_addr |= ENET_QOS_REG_PREP(MAC_MDIO_ADDRESS, CR, (uint32_t)divider);
	base->MAC_MDIO_ADDRESS = mdio_addr;

	return 0;
}

#if IS_ENABLED(CONFIG_MDIO_NXP_ENET_QOS_RUNTIME_CR_UPDATE)
static int mdio_nxp_enet_qos_refresh_cr_if_needed(const struct device *mdio_dev)
{
	const struct nxp_enet_qos_mdio_config *cfg = mdio_dev->config;
	struct nxp_enet_qos_mdio_data *dd = mdio_dev->data;
	const struct nxp_enet_qos_config *enet_cfg = ENET_QOS_MODULE_CFG(cfg->enet_dev);
	uint32_t rate;
	int ret;

	ret = clock_control_get_rate(enet_cfg->clock_dev, enet_cfg->clock_subsys, &rate);
	if (ret != 0) {
		return ret;
	}

	if (dd->last_mdio_clk_hz == rate) {
		return 0;
	}

	ret = mdio_nxp_enet_qos_apply_cr(enet_cfg->base, rate);
	if (ret == 0) {
		dd->last_mdio_clk_hz = rate;
	}

	return ret;
}
#endif

static bool check_busy(enet_qos_t *base)
{
	uint32_t val = base->MAC_MDIO_ADDRESS;

	/* Return the busy bit */
	return ENET_QOS_REG_GET(MAC_MDIO_ADDRESS, GB, val);
}

static int do_transaction(struct mdio_transaction *mdio, bool clause45)
{
	enet_qos_t *base = mdio->base;
	uint8_t goc_1_code;
	int ret;

	k_mutex_lock(mdio->mdio_bus_mutex, K_FOREVER);

#if IS_ENABLED(CONFIG_MDIO_NXP_ENET_QOS_RUNTIME_CR_UPDATE)
	ret = mdio_nxp_enet_qos_refresh_cr_if_needed(mdio->mdio_dev);
	if (ret != 0) {
		goto done;
	}
#endif

	if (clause45) {
		if (mdio->op == MDIO_OP_C45_WRITE) {
			goc_1_code = 0b0;
			base->MAC_MDIO_DATA =
				/* Prepare the data and regaddr to be written */
				ENET_QOS_REG_PREP(MAC_MDIO_DATA, GD, mdio->write_data) |
				ENET_QOS_REG_PREP(MAC_MDIO_DATA, RA, mdio->regaddr_c45);
		} else if (mdio->op == MDIO_OP_C45_READ) {
			goc_1_code = 0b1;
			base->MAC_MDIO_DATA =
				/* Prepare the regaddr to be written */
				ENET_QOS_REG_PREP(MAC_MDIO_DATA, RA, mdio->regaddr_c45);
		} else {
			ret = -EINVAL;
			goto done;
		}
	} else {
		if (mdio->op == MDIO_OP_C22_WRITE) {
			base->MAC_MDIO_DATA =
				/* Prepare the data to be written */
				ENET_QOS_REG_PREP(MAC_MDIO_DATA, GD, mdio->write_data);
			goc_1_code = 0b0;
		} else if (mdio->op == MDIO_OP_C22_READ) {
			goc_1_code = 0b1;
		} else {
			ret = -EINVAL;
			goto done;
		}
	}
	base->MAC_MDIO_ADDRESS &= ~(
		ENET_QOS_REG_PREP(MAC_MDIO_ADDRESS, C45E, 0b1) |
		ENET_QOS_REG_PREP(MAC_MDIO_ADDRESS, GOC_1, 0b1) |
		ENET_QOS_REG_PREP(MAC_MDIO_ADDRESS, GOC_0, 0b1) |
		ENET_QOS_REG_PREP(MAC_MDIO_ADDRESS, PA, 0b11111) |
		ENET_QOS_REG_PREP(MAC_MDIO_ADDRESS, RDA, 0b11111));

	base->MAC_MDIO_ADDRESS |=
		/* C45E */
		ENET_QOS_REG_PREP(MAC_MDIO_ADDRESS, C45E, clause45) |
		/* OP command */
		ENET_QOS_REG_PREP(MAC_MDIO_ADDRESS, GOC_1, goc_1_code) |
		ENET_QOS_REG_PREP(MAC_MDIO_ADDRESS, GOC_0, 0b1) |
		/* PHY address */
		ENET_QOS_REG_PREP(MAC_MDIO_ADDRESS, PA, mdio->portaddr) |
		/* Register address */
		ENET_QOS_REG_PREP(MAC_MDIO_ADDRESS, RDA, mdio->regaddr);

	base->MAC_MDIO_ADDRESS |=
		/* Start the transaction */
		ENET_QOS_REG_PREP(MAC_MDIO_ADDRESS, GB, 0b1);

	ret = -ETIMEDOUT;
	for (int i = CONFIG_MDIO_NXP_ENET_QOS_RECHECK_COUNT; i > 0; i--) {
		if (!check_busy(base)) {
			ret = 0;
			break;
		}
		k_busy_wait(CONFIG_MDIO_NXP_ENET_QOS_RECHECK_TIME);
	}

	if (ret) {
		LOG_ERR("MDIO transaction timed out");
		goto done;
	}

	if (mdio->op == MDIO_OP_C22_READ || mdio->op == MDIO_OP_C45_READ) {
		uint32_t val = mdio->base->MAC_MDIO_DATA;

		*mdio->read_data =
			/* Decipher the read data */
			ENET_QOS_REG_GET(MAC_MDIO_DATA, GD, val);
	}

done:
	k_mutex_unlock(mdio->mdio_bus_mutex);

	return ret;
}

static int nxp_enet_qos_mdio_read(const struct device *dev,
				  uint8_t portaddr, uint8_t regaddr,
				  uint16_t *read_data)
{
	const struct nxp_enet_qos_mdio_config *config = dev->config;
	struct nxp_enet_qos_mdio_data *data = dev->data;
	enet_qos_t *base = ENET_QOS_MODULE_CFG(config->enet_dev)->base;
	struct mdio_transaction mdio_read = {
		.op = MDIO_OP_C22_READ,
		.read_data = read_data,
		.portaddr = portaddr,
		.regaddr = regaddr,
		.base = base,
		.mdio_bus_mutex = &data->mdio_mutex,
		.mdio_dev = dev,
	};

	return do_transaction(&mdio_read, false);
}

static int nxp_enet_qos_mdio_write(const struct device *dev,
				   uint8_t portaddr, uint8_t regaddr,
				   uint16_t write_data)
{
	const struct nxp_enet_qos_mdio_config *config = dev->config;
	struct nxp_enet_qos_mdio_data *data = dev->data;
	enet_qos_t *base = ENET_QOS_MODULE_CFG(config->enet_dev)->base;
	struct mdio_transaction mdio_write = {
		.op = MDIO_OP_C22_WRITE,
		.write_data = write_data,
		.portaddr = portaddr,
		.regaddr = regaddr,
		.base = base,
		.mdio_bus_mutex = &data->mdio_mutex,
		.mdio_dev = dev,
	};

	return do_transaction(&mdio_write, false);
}

static int nxp_enet_qos_mdio_read_c45(const struct device *dev, uint8_t portaddr, uint8_t devaddr,
				      uint16_t regaddr, uint16_t *read_data)
{
	const struct nxp_enet_qos_mdio_config *config = dev->config;
	struct nxp_enet_qos_mdio_data *data = dev->data;
	enet_qos_t *base = ENET_QOS_MODULE_CFG(config->enet_dev)->base;
	struct mdio_transaction mdio_read = {
		.op = MDIO_OP_C45_READ,
		.read_data = read_data,
		.portaddr = portaddr,
		.regaddr = devaddr,
		.regaddr_c45 = regaddr,
		.base = base,
		.mdio_bus_mutex = &data->mdio_mutex,
		.mdio_dev = dev,
	};

	return do_transaction(&mdio_read, true);
}

int nxp_enet_qos_mdio_write_c45(const struct device *dev, uint8_t portaddr, uint8_t devaddr,
				uint16_t regaddr, uint16_t write_data)
{
	const struct nxp_enet_qos_mdio_config *config = dev->config;
	struct nxp_enet_qos_mdio_data *data = dev->data;
	enet_qos_t *base = ENET_QOS_MODULE_CFG(config->enet_dev)->base;
	struct mdio_transaction mdio_write = {
		.op = MDIO_OP_C45_WRITE,
		.write_data = write_data,
		.portaddr = portaddr,
		.regaddr = devaddr,
		.regaddr_c45 = regaddr,
		.base = base,
		.mdio_bus_mutex = &data->mdio_mutex,
		.mdio_dev = dev,
	};

	return do_transaction(&mdio_write, true);
}

static DEVICE_API(mdio, nxp_enet_qos_mdio_api) = {
	.read = nxp_enet_qos_mdio_read,
	.write = nxp_enet_qos_mdio_write,
	.read_c45 = nxp_enet_qos_mdio_read_c45,
	.write_c45 = nxp_enet_qos_mdio_write_c45,
};

static int nxp_enet_qos_mdio_init(const struct device *dev)
{
	const struct nxp_enet_qos_mdio_config *mdio_config = dev->config;
	struct nxp_enet_qos_mdio_data *data = dev->data;
	const struct nxp_enet_qos_config *config = ENET_QOS_MODULE_CFG(mdio_config->enet_dev);
	uint32_t enet_module_clk_rate;
	int ret;

	ret = k_mutex_init(&data->mdio_mutex);
	if (ret != 0) {
		return ret;
	}

	ret = clock_control_get_rate(config->clock_dev, config->clock_subsys,
				     &enet_module_clk_rate);
	if (ret != 0) {
		return ret;
	}

	ret = mdio_nxp_enet_qos_apply_cr(config->base, enet_module_clk_rate);
	if (ret != 0) {
		return ret;
	}

#if IS_ENABLED(CONFIG_MDIO_NXP_ENET_QOS_RUNTIME_CR_UPDATE)
	data->last_mdio_clk_hz = enet_module_clk_rate;
#endif

	return 0;
}

#define NXP_ENET_QOS_MDIO_INIT(inst)						\
										\
	static const struct nxp_enet_qos_mdio_config				\
	nxp_enet_qos_mdio_cfg_##inst = {					\
		.enet_dev = DEVICE_DT_GET(DT_INST_PARENT(inst)),		\
	};									\
										\
	static struct nxp_enet_qos_mdio_data nxp_enet_qos_mdio_data_##inst;	\
										\
	DEVICE_DT_INST_DEFINE(inst, &nxp_enet_qos_mdio_init, NULL,		\
			      &nxp_enet_qos_mdio_data_##inst,			\
			      &nxp_enet_qos_mdio_cfg_##inst,			\
			      POST_KERNEL, CONFIG_MDIO_INIT_PRIORITY,		\
			      &nxp_enet_qos_mdio_api);				\

DT_INST_FOREACH_STATUS_OKAY(NXP_ENET_QOS_MDIO_INIT)
