/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/ztest.h>
#include "regmap.h"

#if !defined(CONFIG_TEST_SENSOR_REGMAP_BASELINE)
static uint32_t values[256];
static double inputs[8];
static bool valid[8];
static struct emul_regmap_data state;
static struct emul model;

static void use_model(const struct emul *source)
{
	memset(&state, 0, sizeof(state));
	memset(valid, 0, sizeof(valid));
	state.values = values;
	state.inputs = inputs;
	state.valid = valid;
	model = (struct emul){.cfg = source->cfg, .data = &state,
		.backend_api = &emul_regmap_sensor_api};
	zassert_ok(emul_regmap_init(&model, NULL));
}

#define USE(node) use_model(EMUL_DT_GET(DT_NODELABEL(node)))

static int transfer(struct i2c_msg *msgs, int count)
{
	return emul_regmap_i2c_api.transfer(&model, msgs, count, 0x48);
}

static void write_bytes(uint8_t *buf, size_t len)
{
	struct i2c_msg msg = {.buf = buf, .len = len, .flags = I2C_MSG_STOP};

	zassert_ok(transfer(&msg, 1));
}

static void write8(uint8_t reg, uint8_t value)
{
	uint8_t buf[] = {reg, value};

	write_bytes(buf, sizeof(buf));
}

static void write16(uint8_t reg, uint16_t value)
{
	uint8_t buf[] = {reg, value >> 8, value & 0xff};

	write_bytes(buf, sizeof(buf));
}

static void read_bytes(uint8_t reg, uint8_t *buf, size_t len)
{
	struct i2c_msg msgs[] = {
		{.buf = &reg, .len = 1},
		{.buf = buf, .len = len, .flags = I2C_MSG_READ | I2C_MSG_RESTART | I2C_MSG_STOP},
	};

	zassert_ok(transfer(msgs, ARRAY_SIZE(msgs)));
}

static uint16_t read16(uint8_t reg)
{
	uint8_t buf[2];

	read_bytes(reg, buf, sizeof(buf));
	return ((uint16_t)buf[0] << 8) | buf[1];
}

static uint8_t read8(uint8_t reg)
{
	uint8_t value;

	read_bytes(reg, &value, 1);
	return value;
}

static void inject(enum sensor_channel channel, double value)
{
	struct sensor_chan_spec ch = {.chan_type = channel};
	q31_t q = value * 8388608;

	zassert_ok(emul_sensor_backend_set_channel(&model, ch, &q, 8));
}

ZTEST(register_model, test_pointer_and_split_write)
{
	uint8_t pointer = 2;
	uint8_t value[] = {0x12, 0x3f};
	uint8_t result[2];
	struct i2c_msg msgs[] = {
		{.buf = &pointer, .len = 1},
		{.buf = value, .len = 2, .flags = I2C_MSG_STOP},
	};
	struct i2c_msg read = {.buf = result, .len = 2, .flags = I2C_MSG_READ | I2C_MSG_STOP};

	USE(p3t);
	zassert_ok(transfer(msgs, 2));
	zassert_ok(transfer(&read, 1));
	zassert_equal(result[0], 0x12);
	zassert_equal(result[1], 0x30);
	zassert_ok(transfer(&read, 1));
	zassert_equal(result[0], 0x12);
	zassert_equal(result[1], 0x30);
}

ZTEST(register_model, test_read_only_and_reserved)
{
	uint8_t address = 0xff;
	uint8_t value;
	struct i2c_msg msgs[] = {
		{.buf = &address, .len = 1},
		{.buf = &value, .len = 1, .flags = I2C_MSG_READ | I2C_MSG_STOP},
	};

	USE(mpu);
	write8(0x75, 0);
	zassert_equal(read8(0x75), 0x68);
	write8(0x1a, 0xff);
	zassert_equal(read8(0x1a), 0x3f);
	zassert_equal(transfer(msgs, 2), -EIO);
	address = 0x13;
	zassert_equal(transfer(msgs, 2), -EIO);
}

ZTEST(register_model, test_invalid_messages)
{
	uint8_t buf[] = {0};
	struct i2c_msg msg = {.buf = buf, .len = 0};

	USE(p3t);
	zassert_equal(transfer(NULL, 1), -EINVAL);
	zassert_equal(transfer(&msg, 0), -EINVAL);
	zassert_equal(transfer(&msg, 1), -EINVAL);
	msg.len = 1;
	msg.buf = NULL;
	zassert_equal(transfer(&msg, 1), -EINVAL);
	msg.buf = buf;
	msg.flags = I2C_MSG_ADDR_10_BITS;
	zassert_equal(transfer(&msg, 1), -EINVAL);
}

ZTEST(register_model, test_p3t1755_power_and_resolution)
{
	USE(p3t);
	zassert_equal(read8(1), 0x28);
	zassert_equal(read16(2), 0x4b00);
	zassert_equal(read16(3), 0x5000);
	for (uint8_t resolution = 0; resolution < 4U; resolution++) {
		write8(1, resolution << 5);
		inject(SENSOR_CHAN_AMBIENT_TEMP, -0.0625);
		zassert_equal(read16(0), 0xfff0);
	}
	write8(1, 1);
	inject(SENSOR_CHAN_AMBIENT_TEMP, 25);
	zassert_equal(read16(0), 0xfff0);
	write8(1, 0x81);
	zassert_equal(read8(1), 1);
	zassert_equal(read16(0), 0x1900);
	inject(SENSOR_CHAN_AMBIENT_TEMP, 30);
	zassert_equal(read16(0), 0x1900);
}

ZTEST(register_model, test_tcn75a_resolution_and_one_shot)
{
	USE(tcn);
	zassert_equal(read16(2), 0x4b00);
	zassert_equal(read16(3), 0x5000);
	for (uint8_t resolution = 0; resolution < 4U; resolution++) {
		write8(1, resolution << 5);
		inject(SENSOR_CHAN_AMBIENT_TEMP, 25.0625);
		zassert_equal(read16(0),
			      resolution == 3U ? 0x1910 : resolution == 2U ? 0x1920 : 0x1900);
	}
	write16(2, 0xffff);
	zassert_equal(read16(2), 0xff80);
	write8(1, 1);
	inject(SENSOR_CHAN_AMBIENT_TEMP, -0.5);
	write8(1, 0x81);
	zassert_equal(read8(1), 1);
	zassert_equal(read16(0), 0xff80);
	inject(SENSOR_CHAN_AMBIENT_TEMP, 40);
	zassert_equal(read16(0), 0xff80);
}

ZTEST(register_model, test_lps22hb_increment_bdu_and_reset)
{
	uint8_t buf[3];

	USE(lps);
	zassert_equal(read8(0x0f), 0xb1);
	zassert_equal(read8(0x11), 0x10);
	inject(SENSOR_CHAN_PRESS, 100);
	read_bytes(0x28, buf, 3);
	zassert_mem_equal(buf, ((uint8_t[]){0, 0, 0}), 3);
	write8(0x11, 0x11);
	zassert_equal(read8(0x11), 0x10);
	read_bytes(0x28, buf, 3);
	zassert_mem_equal(buf, ((uint8_t[]){0, 0x80, 0x3e}), 3);
	write8(0x10, 0x32);
	inject(SENSOR_CHAN_PRESS, 101);
	inject(SENSOR_CHAN_PRESS, 102);
	read_bytes(0x28, buf, 3);
	zassert_mem_equal(buf, ((uint8_t[]){0, 0x20, 0x3f}), 3);
	zassert_equal(read8(0x27) & 1U, 0);
	write8(0x11, 0);
	read_bytes(0x29, buf, 3);
	zassert_mem_equal(buf, ((uint8_t[]){0x20, 0x20, 0x20}), 3);
	write8(0x11, 4);
	zassert_equal(read8(0x10), 0);
	zassert_equal(read8(0x11), 0x10);
}

ZTEST(register_model, test_mpu6050_scale_sleep_status_and_reset)
{
	USE(mpu);
	inject(SENSOR_CHAN_ACCEL_X, 9.80665);
	zassert_equal(read16(0x3b), 0);
	write8(0x6b, 0);
	for (uint8_t fs = 0; fs < 4U; fs++) {
		write8(0x1c, fs << 3);
		inject(SENSOR_CHAN_ACCEL_X, 9.80665);
		zassert_equal(read16(0x3b), 16384U >> fs);
	}
	inject(SENSOR_CHAN_DIE_TEMP, 36.53);
	zassert_equal(read16(0x41), 0);
	zassert_equal(read8(0x3a) & 1U, 1);
	zassert_equal(read8(0x3a), 0);
	write8(0x37, 0x10);
	inject(SENSOR_CHAN_ACCEL_Y, 0);
	read8(0x75);
	zassert_equal(read8(0x3a), 0);
	write8(0x6b, 0x80);
	zassert_equal(read8(0x6b), 0x40);
	zassert_equal(read8(0x1c), 0);
}

ZTEST(register_model, test_tmp11x_status_modes_and_offset)
{
	USE(tmp);
	zassert_equal(read16(0), 0x8000);
	zassert_equal(read16(1), 0x0220);
	write16(1, 0x0400);
	inject(SENSOR_CHAN_AMBIENT_TEMP, -0.5);
	zassert_equal(read16(0), 0x8000);
	write16(1, 0x0c00);
	zassert_equal(read16(1) & 0x2c00U, 0x2400);
	zassert_equal(read16(1) & 0x2000U, 0);
	zassert_equal(read16(0), 0xffc0);
	write16(1, 0x0800);
	zassert_equal(read16(1) & 0x0c00U, 0);
	if (!IS_ENABLED(CONFIG_SENSOR_EMUL_TMP116)) {
		write16(7, 128);
		inject(SENSOR_CHAN_AMBIENT_TEMP, 25);
		zassert_equal(read16(0), 26U * 128U);
		write16(1, 2);
		zassert_equal(read16(1), 0x0220);
	}
}

ZTEST(register_model, test_tmp11x_eeprom_lock)
{
	USE(tmp);
	write16(5, 0x1234);
	zassert_equal(read16(5), 0);
	write16(4, 0x8000);
	write16(5, 0x1234);
	zassert_equal(read16(5), 0x1234);
	write16(4, 0);
	write16(5, 0x5678);
	zassert_equal(read16(5), 0x1234);
}

ZTEST(register_model, test_backend_validation)
{
	struct sensor_chan_spec ch = {.chan_type = SENSOR_CHAN_AMBIENT_TEMP};
	q31_t value = INT32_MAX;

	USE(p3t);
	zassert_equal(emul_sensor_backend_set_channel(&model, ch, &value, 8), -ERANGE);
	zassert_equal(emul_sensor_backend_set_channel(&model, ch, NULL, 8), -EINVAL);
	zassert_equal(emul_sensor_backend_set_channel(&model, ch, &value, -32), -EINVAL);
	ch.chan_idx = 1;
	zassert_equal(emul_sensor_backend_set_channel(&model, ch, &value, 8), -ENOTSUP);
	ch.chan_idx = 0;
	ch.chan_type = SENSOR_CHAN_LIGHT;
	zassert_equal(emul_sensor_backend_set_channel(&model, ch, &value, 8), -ENOTSUP);
}

ZTEST(register_model, test_partial_word_write)
{
	uint8_t high[] = {2, 0x12};
	uint8_t low = 0x30;
	struct i2c_msg msgs[] = {
		{.buf = high, .len = 2},
		{.buf = &low, .len = 1, .flags = I2C_MSG_STOP},
	};

	USE(p3t);
	write_bytes(high, sizeof(high));
	zassert_equal(read16(2), 0x4b00);
	zassert_ok(transfer(msgs, 2));
	zassert_equal(read16(2), 0x1230);
}

ZTEST(register_model, test_invalid_description)
{
	struct emul_regmap_register regs[] = {{.bytes = 5}};
	struct emul_regmap_config cfg = {.registers = regs, .register_count = 1};

	USE(p3t);
	model.cfg = &cfg;
	zassert_equal(emul_regmap_init(&model, NULL), -EINVAL);
	cfg.register_count = 0;
	zassert_equal(emul_regmap_init(&model, NULL), -EINVAL);
	cfg.register_count = 257;
	zassert_equal(emul_regmap_init(&model, NULL), -EINVAL);
}

ZTEST(register_model, test_tmp11x_threshold_flags)
{
	USE(tmp);
	write16(2, 30U * 128U);
	write16(3, 20U * 128U);
	inject(SENSOR_CHAN_AMBIENT_TEMP, 35);
	zassert_equal(read16(1) & 0xe000U, 0xa000);
	zassert_equal(read16(1) & 0xe000U, 0);
	inject(SENSOR_CHAN_AMBIENT_TEMP, 15);
	zassert_equal(read16(1) & 0xe000U, 0x6000);
	write16(1, 0x10);
	inject(SENSOR_CHAN_AMBIENT_TEMP, 35);
	zassert_equal(read16(1) & 0xc000U, 0x8000);
	zassert_equal(read16(1) & 0xc000U, 0x8000);
	inject(SENSOR_CHAN_AMBIENT_TEMP, 25);
	zassert_equal(read16(1) & 0xc000U, 0x8000);
	inject(SENSOR_CHAN_AMBIENT_TEMP, 15);
	zassert_equal(read16(1) & 0xc000U, 0);
}

ZTEST(register_model, test_mpu6050_standby_and_signal_reset)
{
	USE(mpu);
	write8(0x6b, 0);
	inject(SENSOR_CHAN_ACCEL_X, 9.80665);
	write8(0x6c, 0x20);
	inject(SENSOR_CHAN_ACCEL_X, 0);
	zassert_equal(read16(0x3b), 16384);
	write8(0x6a, 1);
	zassert_equal(read8(0x6a), 0);
	zassert_equal(read16(0x3b), 0);
	write8(0x68, 7);
	zassert_equal(read8(0x68), 0);
}

ZTEST_SUITE(register_model, NULL, NULL, NULL, NULL, NULL);
#endif
