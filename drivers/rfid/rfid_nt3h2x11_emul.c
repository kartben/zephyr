/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_nt3h2111

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "rfid_nt3h2x11.h"
#include "rfid_nt3h2x11_emul.h"

#define NT3H2X11_BLOCK_TERMINATOR (NT3H2X11_BLOCK_SRAM + NT3H2X11_SRAM_BLOCKS - 1U)
#define NT3H2X11_NS_I2C_WRITABLE  (NT3H2X11_NS_I2C_LOCKED | NT3H2X11_NS_EEPROM_WR_ERR)

struct nt3h2x11_emul_cfg {
	struct gpio_dt_spec fd_gpio;
};

struct nt3h2x11_emul_data {
	struct k_mutex lock;
	uint8_t sram[NT3H2X11_SRAM_SIZE];
	uint8_t nc;
	uint8_t ns;
	/* Block, or session register when reg_ptr is set, read by the next read */
	uint8_t ptr;
	bool reg_ptr;
	bool field;
	/* Host data read by the reader, signaled on FD in I2C to NFC direction */
	bool rf_read_done;
};

static bool nt3h2x11_emul_pthru(const struct nt3h2x11_emul_data *data, bool nfc_to_i2c)
{
	const uint8_t mask = NT3H2X11_NC_PTHRU_ON_OFF | NT3H2X11_NC_TRANSFER_DIR;
	const uint8_t want =
		NT3H2X11_NC_PTHRU_ON_OFF | (nfc_to_i2c ? NT3H2X11_NC_TRANSFER_DIR : 0U);

	return (data->nc & mask) == want;
}

static bool nt3h2x11_emul_fd_asserted(const struct nt3h2x11_emul_data *data)
{
	if (!data->field) {
		return false;
	}

	switch (FIELD_GET(NT3H2X11_NC_FD_ON, data->nc)) {
	case 0U:
		return true;
	case 3U:
		if (nt3h2x11_emul_pthru(data, true)) {
			return (data->ns & NT3H2X11_NS_SRAM_I2C_READY) != 0U;
		}
		if (nt3h2x11_emul_pthru(data, false)) {
			return data->rf_read_done;
		}
		return false;
	default:
		return false;
	}
}

/* Called with the lock held, after any state change */
static void nt3h2x11_emul_update(const struct emul *target)
{
	const struct nt3h2x11_emul_cfg *cfg = target->cfg;
	struct nt3h2x11_emul_data *data = target->data;
	gpio_flags_t flags;
	bool fd;

	if (!data->field) {
		data->nc &= ~NT3H2X11_NC_PTHRU_ON_OFF;
	}

	if ((data->nc & NT3H2X11_NC_PTHRU_ON_OFF) == 0U) {
		data->ns &= ~(NT3H2X11_NS_SRAM_I2C_READY | NT3H2X11_NS_SRAM_RF_READY);
		data->rf_read_done = false;
	}

	/* Drive FD once the driver has made it an input; edges fire on changes only */
	if ((cfg->fd_gpio.port == NULL) || (gpio_emul_flags_get_dt(&cfg->fd_gpio, &flags) < 0) ||
	    ((flags & GPIO_INPUT) == 0U)) {
		return;
	}

	fd = nt3h2x11_emul_fd_asserted(data);
	(void)gpio_emul_input_set_dt(&cfg->fd_gpio,
				     ((cfg->fd_gpio.dt_flags & GPIO_ACTIVE_LOW) != 0U) ? !fd : fd);
}

static uint8_t nt3h2x11_emul_reg_read(const struct nt3h2x11_emul_data *data, uint8_t reg)
{
	switch (reg) {
	case NT3H2X11_REG_NC:
		return data->nc;
	case NT3H2X11_REG_NS:
		return data->ns | (data->field ? NT3H2X11_NS_RF_FIELD_PRESENT : 0U);
	default:
		return 0U;
	}
}

static void nt3h2x11_emul_reg_write(struct nt3h2x11_emul_data *data, uint8_t reg, uint8_t mask,
				    uint8_t val)
{
	switch (reg) {
	case NT3H2X11_REG_NC:
		data->nc = (data->nc & ~mask) | (val & mask);
		break;
	case NT3H2X11_REG_NS:
		mask &= NT3H2X11_NS_I2C_WRITABLE;
		data->ns = (data->ns & ~mask) | (val & mask);
		break;
	default:
		break;
	}
}

static int nt3h2x11_emul_write(const struct emul *target, const uint8_t *buf, uint32_t len)
{
	struct nt3h2x11_emul_data *data = target->data;
	uint8_t block;

	if (len == 0U) {
		return -EIO;
	}

	block = buf[0];

	if (block == NT3H2X11_BLOCK_SESSION) {
		if (len == 2U) {
			data->ptr = buf[1];
			data->reg_ptr = true;
		} else if (len == 4U) {
			nt3h2x11_emul_reg_write(data, buf[1], buf[2], buf[3]);
		} else {
			return -EIO;
		}
		return 0;
	}

	if (len == 1U) {
		data->ptr = block;
		data->reg_ptr = false;
		return 0;
	}

	if (len != (1U + NT3H2X11_BLOCK_SIZE)) {
		return -EIO;
	}

	if ((block >= NT3H2X11_BLOCK_SRAM) && (block <= NT3H2X11_BLOCK_TERMINATOR)) {
		memcpy(&data->sram[(block - NT3H2X11_BLOCK_SRAM) * NT3H2X11_BLOCK_SIZE], &buf[1],
		       NT3H2X11_BLOCK_SIZE);

		if ((block == NT3H2X11_BLOCK_TERMINATOR) && nt3h2x11_emul_pthru(data, false)) {
			data->ns |= NT3H2X11_NS_SRAM_RF_READY;
			data->rf_read_done = false;
		}
	}

	return 0;
}

static int nt3h2x11_emul_read(const struct emul *target, uint8_t *buf, uint32_t len)
{
	struct nt3h2x11_emul_data *data = target->data;
	uint8_t block = data->ptr;

	if (data->reg_ptr) {
		if (len != 1U) {
			return -EIO;
		}
		buf[0] = nt3h2x11_emul_reg_read(data, data->ptr);
		return 0;
	}

	if (len != NT3H2X11_BLOCK_SIZE) {
		return -EIO;
	}

	if ((block >= NT3H2X11_BLOCK_SRAM) && (block <= NT3H2X11_BLOCK_TERMINATOR)) {
		memcpy(buf, &data->sram[(block - NT3H2X11_BLOCK_SRAM) * NT3H2X11_BLOCK_SIZE],
		       NT3H2X11_BLOCK_SIZE);

		if ((block == NT3H2X11_BLOCK_TERMINATOR) && nt3h2x11_emul_pthru(data, true)) {
			data->ns &= ~NT3H2X11_NS_SRAM_I2C_READY;
		}
	} else {
		memset(buf, 0, NT3H2X11_BLOCK_SIZE);
	}

	return 0;
}

static int nt3h2x11_emul_transfer(const struct emul *target, struct i2c_msg *msgs, int num_msgs,
				  int addr)
{
	struct nt3h2x11_emul_data *data = target->data;
	int ret = 0;

	ARG_UNUSED(addr);

	k_mutex_lock(&data->lock, K_FOREVER);

	for (int i = 0; (i < num_msgs) && (ret == 0); i++) {
		if ((msgs[i].flags & I2C_MSG_READ) != 0U) {
			ret = nt3h2x11_emul_read(target, msgs[i].buf, msgs[i].len);
		} else {
			ret = nt3h2x11_emul_write(target, msgs[i].buf, msgs[i].len);
		}
	}

	nt3h2x11_emul_update(target);

	k_mutex_unlock(&data->lock);

	return ret;
}

void nt3h2x11_emul_set_field(const struct emul *target, bool on)
{
	struct nt3h2x11_emul_data *data = target->data;

	k_mutex_lock(&data->lock, K_FOREVER);
	data->field = on;
	nt3h2x11_emul_update(target);
	k_mutex_unlock(&data->lock);
}

int nt3h2x11_emul_reader_write(const struct emul *target, const uint8_t *buf)
{
	struct nt3h2x11_emul_data *data = target->data;
	int ret = -EAGAIN;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (nt3h2x11_emul_pthru(data, true) &&
	    ((data->ns & NT3H2X11_NS_SRAM_I2C_READY) == 0U)) {
		memcpy(data->sram, buf, NT3H2X11_SRAM_SIZE);
		data->ns |= NT3H2X11_NS_SRAM_I2C_READY;
		nt3h2x11_emul_update(target);
		ret = 0;
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

int nt3h2x11_emul_reader_read(const struct emul *target, uint8_t *buf)
{
	struct nt3h2x11_emul_data *data = target->data;
	int ret = -EAGAIN;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (nt3h2x11_emul_pthru(data, false) && ((data->ns & NT3H2X11_NS_SRAM_RF_READY) != 0U)) {
		memcpy(buf, data->sram, NT3H2X11_SRAM_SIZE);
		data->ns &= ~NT3H2X11_NS_SRAM_RF_READY;
		data->rf_read_done = true;
		nt3h2x11_emul_update(target);
		ret = 0;
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

static const struct i2c_emul_api nt3h2x11_emul_api = {
	.transfer = nt3h2x11_emul_transfer,
};

static int nt3h2x11_emul_init(const struct emul *target, const struct device *parent)
{
	struct nt3h2x11_emul_data *data = target->data;

	ARG_UNUSED(parent);

	k_mutex_init(&data->lock);
	/* Configuration register defaults loaded at power-on */
	data->nc = NT3H2X11_NC_TRANSFER_DIR;

	return 0;
}

#define NT3H2X11_EMUL_DEFINE(inst)                                                                 \
	static struct nt3h2x11_emul_data nt3h2x11_emul_data_##inst;                                \
	static const struct nt3h2x11_emul_cfg nt3h2x11_emul_cfg_##inst = {                         \
		.fd_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, fd_gpios, {0}),                          \
	};                                                                                         \
	EMUL_DT_INST_DEFINE(inst, nt3h2x11_emul_init, &nt3h2x11_emul_data_##inst,                  \
			    &nt3h2x11_emul_cfg_##inst, &nt3h2x11_emul_api, NULL);

DT_INST_FOREACH_STATUS_OKAY(NT3H2X11_EMUL_DEFINE)
