/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT goertek_spa06

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/drivers/spi_emul.h>
#include <zephyr/sys/byteorder.h>

#include "spa06.h"
#include "spa06_emul.h"

#define SPA06_EMUL_NUM_REGS   (SPA06_REG_COEF + SPA06_COEF_LEN)
#define SPA06_EMUL_ID         0x11U
#define SPA06_EMUL_CTRL_MASK  GENMASK(2, 0)
#define SPA06_EMUL_STATUS     GENMASK(7, 4)
#define SPA06_EMUL_FIFO_EMPTY BIT(0)
#define SPA06_EMUL_FIFO_FULL  BIT(1)

struct spa06_emul_data {
	uint8_t reg[SPA06_EMUL_NUM_REGS];
	int32_t raw_prs;
	int32_t raw_tmp;
	uint32_t fifo[SPA06_FIFO_SIZE];
	uint8_t fifo_count;
};

/* A sensor reading about 100 kPa and 25 degrees Celsius for raw values near zero */
static const struct spa06_coef spa06_emul_default_coef = {
	.c0 = 50,
	.c1 = -260,
	.c00 = 100000,
	.c10 = -50000,
	.c01 = -1000,
	.c11 = 1000,
	.c20 = -5000,
	.c21 = 100,
	.c30 = -500,
	.c31 = 10,
	.c40 = 5,
};

void spa06_emul_set_coef(const struct emul *target, const struct spa06_coef *coef)
{
	struct spa06_emul_data *data = target->data;
	uint8_t *buf = &data->reg[SPA06_REG_COEF];
	uint32_t c0 = (uint32_t)coef->c0 & 0xFFFU;
	uint32_t c1 = (uint32_t)coef->c1 & 0xFFFU;
	uint32_t c00 = (uint32_t)coef->c00 & 0xFFFFFU;
	uint32_t c10 = (uint32_t)coef->c10 & 0xFFFFFU;
	uint32_t c31 = (uint32_t)coef->c31 & 0xFFFU;
	uint32_t c40 = (uint32_t)coef->c40 & 0xFFFU;

	buf[0] = (uint8_t)(c0 >> 4);
	buf[1] = (uint8_t)(((c0 & 0xFU) << 4) | (c1 >> 8));
	buf[2] = (uint8_t)c1;
	buf[3] = (uint8_t)(c00 >> 12);
	buf[4] = (uint8_t)(c00 >> 4);
	buf[5] = (uint8_t)(((c00 & 0xFU) << 4) | (c10 >> 16));
	sys_put_be16((uint16_t)c10, &buf[6]);
	sys_put_be16((uint16_t)coef->c01, &buf[8]);
	sys_put_be16((uint16_t)coef->c11, &buf[10]);
	sys_put_be16((uint16_t)coef->c20, &buf[12]);
	sys_put_be16((uint16_t)coef->c21, &buf[14]);
	sys_put_be16((uint16_t)coef->c30, &buf[16]);
	buf[18] = (uint8_t)(c31 >> 4);
	buf[19] = (uint8_t)(((c31 & 0xFU) << 4) | (c40 >> 8));
	buf[20] = (uint8_t)c40;
}

static void spa06_emul_measure(struct spa06_emul_data *data, bool prs, bool tmp)
{
	if (prs) {
		sys_put_be24((uint32_t)data->raw_prs, &data->reg[SPA06_REG_PSR_B2]);
		data->reg[SPA06_REG_MEAS_CFG] |= SPA06_MEAS_CFG_PRS_RDY;
	}

	if (tmp) {
		sys_put_be24((uint32_t)data->raw_tmp, &data->reg[SPA06_REG_PSR_B2 + 3U]);
		data->reg[SPA06_REG_MEAS_CFG] |= SPA06_MEAS_CFG_TMP_RDY;
	}
}

void spa06_emul_set_raw(const struct emul *target, int32_t raw_prs, int32_t raw_tmp)
{
	struct spa06_emul_data *data = target->data;
	uint8_t ctrl = data->reg[SPA06_REG_MEAS_CFG] & SPA06_EMUL_CTRL_MASK;

	data->raw_prs = raw_prs;
	data->raw_tmp = raw_tmp;

	if (ctrl == SPA06_MEAS_CTRL_CONTINUOUS) {
		spa06_emul_measure(data, true, true);
	}
}

int spa06_emul_fifo_push(const struct emul *target, uint32_t entry)
{
	struct spa06_emul_data *data = target->data;

	if (data->fifo_count >= SPA06_FIFO_SIZE) {
		return -ENOSPC;
	}

	data->fifo[data->fifo_count] = entry & 0xFFFFFFU;
	data->fifo_count++;

	return 0;
}

void spa06_emul_set_int_status(const struct emul *target, uint8_t int_sts)
{
	struct spa06_emul_data *data = target->data;

	data->reg[SPA06_REG_INT_STS] = int_sts;
}

uint8_t spa06_emul_get_reg(const struct emul *target, uint8_t reg)
{
	const struct spa06_emul_data *data = target->data;

	return (reg < SPA06_EMUL_NUM_REGS) ? data->reg[reg] : 0U;
}

static void spa06_emul_reset(struct spa06_emul_data *data)
{
	uint8_t coef[SPA06_COEF_LEN];

	memcpy(coef, &data->reg[SPA06_REG_COEF], sizeof(coef));
	memset(data->reg, 0, sizeof(data->reg));
	memcpy(&data->reg[SPA06_REG_COEF], coef, sizeof(coef));

	data->reg[SPA06_REG_ID] = SPA06_EMUL_ID;
	data->reg[SPA06_REG_MEAS_CFG] = SPA06_MEAS_CFG_COEF_RDY | SPA06_MEAS_CFG_SENSOR_RDY;
	data->fifo_count = 0U;
}

static void spa06_emul_write(struct spa06_emul_data *data, uint8_t reg, uint8_t val)
{
	switch (reg) {
	case SPA06_REG_MEAS_CFG:
		data->reg[reg] = (data->reg[reg] & SPA06_EMUL_STATUS) | (val & ~SPA06_EMUL_STATUS);

		switch (val & SPA06_EMUL_CTRL_MASK) {
		case SPA06_MEAS_CTRL_PRS:
			/* Command measurements return to standby when done */
			spa06_emul_measure(data, true, false);
			data->reg[reg] &= ~SPA06_EMUL_CTRL_MASK;
			break;
		case SPA06_MEAS_CTRL_TMP:
			spa06_emul_measure(data, false, true);
			data->reg[reg] &= ~SPA06_EMUL_CTRL_MASK;
			break;
		case SPA06_MEAS_CTRL_CONTINUOUS:
			spa06_emul_measure(data, true, true);
			break;
		default:
			break;
		}
		break;
	case SPA06_REG_RESET:
		if ((val & 0x0FU) == SPA06_RESET_SOFT_RST) {
			spa06_emul_reset(data);
		}

		if ((val & SPA06_RESET_FIFO_FLUSH) != 0U) {
			data->fifo_count = 0U;
		}
		break;
	case SPA06_REG_INT_STS:
	case SPA06_REG_FIFO_STS:
	case SPA06_REG_ID:
		break;
	default:
		if (reg < SPA06_REG_COEF) {
			data->reg[reg] = val;
		}
		break;
	}
}

static bool spa06_emul_overlaps(uint8_t start, size_t len, uint8_t first, uint8_t last)
{
	return (start <= last) && ((start + len) > first);
}

static void spa06_emul_read(struct spa06_emul_data *data, uint8_t start, uint8_t *buf, size_t len)
{
	/* With the FIFO enabled, each read of the pressure result returns the next entry */
	if (((data->reg[SPA06_REG_CFG_REG] & SPA06_CFG_REG_FIFO_EN) != 0U) &&
	    (start == SPA06_REG_PSR_B2)) {
		uint32_t entry = SPA06_FIFO_EMPTY;

		if (data->fifo_count > 0U) {
			entry = data->fifo[0];
			data->fifo_count--;
			memmove(&data->fifo[0], &data->fifo[1],
				data->fifo_count * sizeof(data->fifo[0]));
		}

		sys_put_be24(entry, &data->reg[SPA06_REG_PSR_B2]);
	}

	data->reg[SPA06_REG_FIFO_STS] = 0U;
	if (data->fifo_count == 0U) {
		data->reg[SPA06_REG_FIFO_STS] |= SPA06_EMUL_FIFO_EMPTY;
	}
	if (data->fifo_count == SPA06_FIFO_SIZE) {
		data->reg[SPA06_REG_FIFO_STS] |= SPA06_EMUL_FIFO_FULL;
	}

	for (size_t i = 0; i < len; i++) {
		size_t reg = start + i;

		buf[i] = (reg < SPA06_EMUL_NUM_REGS) ? data->reg[reg] : 0U;
	}

	/* Reading a result clears its ready flag, reading INT_STS clears it */
	if (spa06_emul_overlaps(start, len, 0x00U, 0x02U)) {
		data->reg[SPA06_REG_MEAS_CFG] &= ~SPA06_MEAS_CFG_PRS_RDY;
	}

	if (spa06_emul_overlaps(start, len, 0x03U, 0x05U)) {
		data->reg[SPA06_REG_MEAS_CFG] &= ~SPA06_MEAS_CFG_TMP_RDY;
	}

	if (spa06_emul_overlaps(start, len, SPA06_REG_INT_STS, SPA06_REG_INT_STS)) {
		data->reg[SPA06_REG_INT_STS] = 0U;
	}
}

#if SPA06_BUS_I2C
static int spa06_emul_transfer_i2c(const struct emul *target, struct i2c_msg *msgs, int num_msgs,
				   int addr)
{
	struct spa06_emul_data *data = target->data;
	uint8_t reg;

	ARG_UNUSED(addr);

	if ((num_msgs < 1) || ((msgs[0].flags & I2C_MSG_READ) != 0U) || (msgs[0].len < 1U)) {
		return -EIO;
	}

	/* The first message carries the register address, possibly followed by data */
	reg = msgs[0].buf[0];
	for (uint32_t i = 1U; i < msgs[0].len; i++) {
		spa06_emul_write(data, reg++, msgs[0].buf[i]);
	}

	for (int m = 1; m < num_msgs; m++) {
		if ((msgs[m].flags & I2C_MSG_READ) != 0U) {
			spa06_emul_read(data, reg, msgs[m].buf, msgs[m].len);
		} else {
			for (uint32_t i = 0U; i < msgs[m].len; i++) {
				spa06_emul_write(data, reg++, msgs[m].buf[i]);
			}
		}
	}

	return 0;
}

static const struct i2c_emul_api spa06_emul_api_i2c = {
	.transfer = spa06_emul_transfer_i2c,
};
#endif /* SPA06_BUS_I2C */

#if SPA06_BUS_SPI
static int spa06_emul_io_spi(const struct emul *target, const struct spi_config *config,
			     const struct spi_buf_set *tx_bufs, const struct spi_buf_set *rx_bufs)
{
	struct spa06_emul_data *data = target->data;
	const uint8_t *tx;
	size_t tx_len;

	ARG_UNUSED(config);

	if ((tx_bufs == NULL) || (tx_bufs->count < 1U) || (tx_bufs->buffers[0].len < 1U)) {
		return -EIO;
	}

	tx = tx_bufs->buffers[0].buf;
	tx_len = tx_bufs->buffers[0].len;

	if ((tx[0] & SPA06_SPI_READ) != 0U) {
		/* The address byte is clocked in while the first RX buffer is skipped */
		if ((rx_bufs == NULL) || (rx_bufs->count != 2U)) {
			return -EIO;
		}

		spa06_emul_read(data, tx[0] & (uint8_t)~SPA06_SPI_READ, rx_bufs->buffers[1].buf,
				rx_bufs->buffers[1].len);
		return 0;
	}

	/* Writes are pairs of address and data bytes */
	for (size_t i = 0; (i + 1U) < tx_len; i += 2U) {
		spa06_emul_write(data, tx[i] & (uint8_t)~SPA06_SPI_READ, tx[i + 1U]);
	}

	return 0;
}

static struct spi_emul_api spa06_emul_api_spi = {
	.io = spa06_emul_io_spi,
};
#endif /* SPA06_BUS_SPI */

static int spa06_emul_init(const struct emul *target, const struct device *parent)
{
	ARG_UNUSED(parent);

	spa06_emul_set_coef(target, &spa06_emul_default_coef);
	spa06_emul_reset(target->data);

	return 0;
}

#define SPA06_EMUL(n)                                                                              \
	static struct spa06_emul_data spa06_emul_data_##n;                                         \
	EMUL_DT_INST_DEFINE(n, spa06_emul_init, &spa06_emul_data_##n, NULL,                        \
			    COND_CODE_1(DT_INST_ON_BUS(n, spi), (&spa06_emul_api_spi),             \
					(&spa06_emul_api_i2c)), NULL)

DT_INST_FOREACH_STATUS_OKAY(SPA06_EMUL)
