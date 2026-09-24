/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT m5stack_m5ioe1

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/mfd/m5ioe1.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(mfd_m5ioe1, CONFIG_MFD_LOG_LEVEL);

#define M5IOE1_REG_UID_L 0x00
#define M5IOE1_REG_REV   0x02

#define M5IOE1_BOOT_POLL_MS 10

struct m5ioe1_config {
	struct i2c_dt_spec i2c;
};

struct m5ioe1_data {
	struct k_mutex lock;
};

int mfd_m5ioe1_burst_read(const struct device *dev, uint8_t reg, uint8_t *buf, size_t len)
{
	const struct m5ioe1_config *config = dev->config;
	struct m5ioe1_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = i2c_burst_read_dt(&config->i2c, reg, buf, len);
	k_mutex_unlock(&data->lock);

	return ret;
}

int mfd_m5ioe1_burst_write(const struct device *dev, uint8_t reg, const uint8_t *buf, size_t len)
{
	const struct m5ioe1_config *config = dev->config;
	struct m5ioe1_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = i2c_burst_write_dt(&config->i2c, reg, buf, len);
	k_mutex_unlock(&data->lock);

	return ret;
}

int mfd_m5ioe1_read_reg16(const struct device *dev, uint8_t reg, uint16_t *val)
{
	uint8_t buf[2];
	int ret;

	ret = mfd_m5ioe1_burst_read(dev, reg, buf, sizeof(buf));
	if (ret < 0) {
		return ret;
	}

	*val = sys_get_le16(buf);

	return 0;
}

/* Replace the bits in @p mask with @p set, then invert the bits in @p flip. */
static int m5ioe1_modify_reg16(const struct device *dev, uint8_t reg, uint16_t mask, uint16_t set,
			       uint16_t flip)
{
	const struct m5ioe1_config *config = dev->config;
	struct m5ioe1_data *data = dev->data;
	uint8_t buf[2];
	uint16_t val;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	ret = i2c_burst_read_dt(&config->i2c, reg, buf, sizeof(buf));
	if (ret < 0) {
		goto out;
	}

	val = ((sys_get_le16(buf) & ~mask) | (set & mask)) ^ flip;
	sys_put_le16(val, buf);

	ret = i2c_burst_write_dt(&config->i2c, reg, buf, sizeof(buf));

out:
	k_mutex_unlock(&data->lock);

	return ret;
}

int mfd_m5ioe1_update_reg16(const struct device *dev, uint8_t reg, uint16_t mask, uint16_t val)
{
	return m5ioe1_modify_reg16(dev, reg, mask, val, 0U);
}

int mfd_m5ioe1_toggle_reg16(const struct device *dev, uint8_t reg, uint16_t mask)
{
	return m5ioe1_modify_reg16(dev, reg, 0U, 0U, mask);
}

static int m5ioe1_init(const struct device *dev)
{
	const struct m5ioe1_config *config = dev->config;
	struct m5ioe1_data *data = dev->data;
	k_timepoint_t timeout;
	uint8_t id[3];
	int ret;

	if (!i2c_is_ready_dt(&config->i2c)) {
		LOG_ERR_DEVICE_NOT_READY(config->i2c.bus);
		return -ENODEV;
	}

	k_mutex_init(&data->lock);

	/* Firmware revision reads back as 0x00 or 0xff until the device has booted. */
	timeout = sys_timepoint_calc(K_MSEC(CONFIG_MFD_M5IOE1_BOOT_TIMEOUT_MS));
	do {
		ret = i2c_burst_read_dt(&config->i2c, M5IOE1_REG_UID_L, id, sizeof(id));
		if (ret == 0 && id[2] != 0x00U && id[2] != 0xffU) {
			break;
		}
		ret = -ENODEV;
		k_msleep(M5IOE1_BOOT_POLL_MS);
	} while (!sys_timepoint_expired(timeout));

	if (ret < 0) {
		LOG_ERR("M5IOE1 not responding");
		return ret;
	}

	LOG_DBG("UID 0x%04x, firmware revision 0x%02x", sys_get_le16(id), id[2]);

	return 0;
}

#define M5IOE1_DEFINE(inst)                                                                        \
	static const struct m5ioe1_config m5ioe1_config_##inst = {                                 \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                 \
	};                                                                                         \
	static struct m5ioe1_data m5ioe1_data_##inst;                                              \
	DEVICE_DT_INST_DEFINE(inst, m5ioe1_init, NULL, &m5ioe1_data_##inst,                        \
			      &m5ioe1_config_##inst, POST_KERNEL, CONFIG_MFD_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(M5IOE1_DEFINE)
