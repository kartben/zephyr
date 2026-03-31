/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Microchip RNG90 I2C secure RNG (DS40002499A). CRC matches CryptoAuthLib atCRC().
 */

#define DT_DRV_COMPAT microchip_rng90

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/entropy.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(entropy_rng90, CONFIG_ENTROPY_LOG_LEVEL);

#define RNG90_WORD_ADDR_CMD 0x03U
#define RNG90_OPCODE_RANDOM 0x16U
#define RNG90_RANDOM_PKT_LEN 24U
/* Count byte value: count + packet(24) + CRC(2) */
#define RNG90_RANDOM_COUNT_VAL (1U + RNG90_RANDOM_PKT_LEN + 2U)
/* Word address + count + packet + CRC */
#define RNG90_RANDOM_TX_LEN    (1U + RNG90_RANDOM_COUNT_VAL)
#define RNG90_RANDOM_OUT_CNT   (1U + 32U + 2U)

#define RNG90_POLL_TIMEOUT_FIRST_MS  100U
#define RNG90_POLL_TIMEOUT_NEXT_MS   40U

struct rng90_config {
	struct i2c_dt_spec i2c;
};

struct rng90_data {
	struct k_mutex lock;
	bool awake;
	bool first_random;
};

/* Same algorithm as Microchip CryptoAuthLib atCRC() (calib_command.c). */
static void rng90_at_crc(size_t length, const uint8_t *data, uint8_t *crc_le)
{
	uint16_t crc_register = 0;
	const uint16_t polynom = 0x8005U;

	for (size_t counter = 0; counter < length; counter++) {
		for (uint8_t shift_register = 0x01U; shift_register != 0U; shift_register <<= 1) {
			uint8_t data_bit = ((data[counter] & shift_register) != 0U) ? 1U : 0U;
			uint8_t crc_bit = (uint8_t)(crc_register >> 15);

			crc_register <<= 1;
			if (data_bit != crc_bit) {
				crc_register ^= polynom;
			}
		}
	}

	crc_le[0] = (uint8_t)(crc_register & 0x00FFU);
	crc_le[1] = (uint8_t)(crc_register >> 8);
}

static bool rng90_crc_ok(size_t len, const uint8_t *buf)
{
	uint8_t calc[2];

	if (len < 4U) {
		return false;
	}

	rng90_at_crc(len - 2U, buf, calc);

	return (buf[len - 2U] == calc[0]) && (buf[len - 1U] == calc[1]);
}

static bool rng90_i2c_busy(int ret)
{
	return (ret == -EIO) || (ret == -EFAULT) || (ret == -EAGAIN) || (ret == -ENXIO);
}

static int rng90_wake(const struct i2c_dt_spec *i2c)
{
	static uint8_t wake_buf;
	int ret;

	/* Wake: START + addr(W) + NACK + STOP; tPU >= 1 ms */
	ret = i2c_write_dt(i2c, &wake_buf, 0);
	if ((ret != 0) && !rng90_i2c_busy(ret)) {
		return ret;
	}

	k_sleep(K_MSEC(2));
	return 0;
}

static int rng90_poll_read(const struct i2c_dt_spec *i2c, uint8_t *buf, size_t len,
			   uint32_t timeout_ms)
{
	uint64_t deadline = k_uptime_get() + timeout_ms;

	do {
		int ret = i2c_read_dt(i2c, buf, len);

		if (ret == 0) {
			return 0;
		}

		if (!rng90_i2c_busy(ret)) {
			return ret;
		}

		if (k_uptime_get() >= deadline) {
			return ret;
		}

		k_usleep(200);
	} while (true);
}

static int rng90_send_random(const struct i2c_dt_spec *i2c)
{
	uint8_t pkt[RNG90_RANDOM_PKT_LEN];
	uint8_t tx[RNG90_RANDOM_TX_LEN];
	uint8_t crc[2];

	pkt[0] = RNG90_OPCODE_RANDOM;
	pkt[1] = 0x00U;
	pkt[2] = 0x00U;
	pkt[3] = 0x00U;
	(void)memset(&pkt[4], 0, 20U);

	tx[0] = RNG90_WORD_ADDR_CMD;
	tx[1] = RNG90_RANDOM_COUNT_VAL;
	(void)memcpy(&tx[2], pkt, RNG90_RANDOM_PKT_LEN);

	rng90_at_crc(1U + RNG90_RANDOM_PKT_LEN, &tx[1], crc);
	tx[1U + RNG90_RANDOM_COUNT_VAL - 2U] = crc[0];
	tx[1U + RNG90_RANDOM_COUNT_VAL - 1U] = crc[1];

	return i2c_write_dt(i2c, tx, sizeof(tx));
}

static int rng90_random(const struct i2c_dt_spec *i2c, uint8_t *out32, bool first,
			struct rng90_data *data)
{
	uint8_t rsp[35];
	uint32_t timeout_ms = first ? RNG90_POLL_TIMEOUT_FIRST_MS : RNG90_POLL_TIMEOUT_NEXT_MS;
	int ret;

	ret = rng90_send_random(i2c);
	if (ret != 0) {
		LOG_DBG("RNG90 write failed: %d", ret);
		return ret;
	}

	ret = rng90_poll_read(i2c, rsp, sizeof(rsp), timeout_ms);
	if (ret != 0) {
		LOG_DBG("RNG90 read poll failed: %d", ret);
		return ret;
	}

	if (rsp[0] == 4U) {
		if (!rng90_crc_ok(4U, rsp)) {
			return -EIO;
		}

		data->awake = false;
		LOG_WRN("RNG90 error status 0x%02x", rsp[1]);
		return -EIO;
	}

	if (rsp[0] != RNG90_RANDOM_OUT_CNT) {
		LOG_WRN("RNG90 unexpected count %u", rsp[0]);
		return -EIO;
	}

	if (!rng90_crc_ok(RNG90_RANDOM_OUT_CNT, rsp)) {
		LOG_WRN("RNG90 response CRC mismatch");
		return -EIO;
	}

	(void)memcpy(out32, &rsp[1], 32U);
	return 0;
}

static int rng90_get_entropy(const struct device *dev, uint8_t *buffer, uint16_t length)
{
	const struct rng90_config *cfg = dev->config;
	struct rng90_data *data = dev->data;
	int ret = 0;

	if (length == 0U) {
		return 0;
	}

	(void)k_mutex_lock(&data->lock, K_FOREVER);

	if (!data->awake) {
		ret = rng90_wake(&cfg->i2c);
		if (ret != 0) {
			goto out;
		}
		data->awake = true;
	}

	for (uint16_t off = 0; off < length; off += 32U) {
		uint8_t block[32];
		uint16_t chunk = MIN(32U, length - off);

		ret = rng90_random(&cfg->i2c, block, data->first_random, data);
		if (ret != 0) {
			data->awake = false;
			goto out;
		}

		data->first_random = false;
		(void)memcpy(buffer + off, block, chunk);
	}

out:
	(void)k_mutex_unlock(&data->lock);
	return ret;
}

static int rng90_init(const struct device *dev)
{
	struct rng90_data *data = dev->data;

	k_mutex_init(&data->lock);
	data->awake = false;
	data->first_random = true;

	return 0;
}

static DEVICE_API(entropy, rng90_driver_api) = {
	.get_entropy = rng90_get_entropy,
};

#define RNG90_DEFINE(inst)                                                                         \
	static const struct rng90_config rng90_config_##inst = {                                     \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                 \
	};                                                                                         \
	static struct rng90_data rng90_data_##inst;                                                \
	DEVICE_DT_INST_DEFINE(inst, rng90_init, NULL, &rng90_data_##inst, &rng90_config_##inst,      \
			      POST_KERNEL, CONFIG_ENTROPY_INIT_PRIORITY, &rng90_driver_api);

DT_INST_FOREACH_STATUS_OKAY(RNG90_DEFINE)
