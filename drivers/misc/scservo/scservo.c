/*
 * Copyright (c) 2026 Your Name
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT feetech_scservo

#include <zephyr/drivers/misc/scservo/scservo.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(scservo, CONFIG_SCSERVO_LOG_LEVEL);

/* SCServo protocol constants */
#define SCSERVO_HEADER_BYTE        0xFF
#define SCSERVO_BROADCAST_ID       0xFE

/* SCServo instruction set */
#define INST_PING                  0x01
#define INST_READ                  0x02
#define INST_WRITE                 0x03
#define INST_REG_WRITE             0x04
#define INST_REG_ACTION            0x05
#define INST_SYNC_READ             0x82
#define INST_SYNC_WRITE            0x83

/* SCSCL Register addresses (SRAM - read/write) */
#define SCSCL_TORQUE_ENABLE        40
#define SCSCL_GOAL_POSITION_L      42
#define SCSCL_GOAL_POSITION_H      43
#define SCSCL_GOAL_TIME_L          44
#define SCSCL_GOAL_TIME_H          45
#define SCSCL_GOAL_SPEED_L         46
#define SCSCL_GOAL_SPEED_H         47

/* SCSCL Register addresses (SRAM - read only) */
#define SCSCL_PRESENT_POSITION_L   56
#define SCSCL_PRESENT_POSITION_H   57
#define SCSCL_PRESENT_SPEED_L      58
#define SCSCL_PRESENT_SPEED_H      59
#define SCSCL_PRESENT_LOAD_L       60
#define SCSCL_PRESENT_LOAD_H       61
#define SCSCL_PRESENT_VOLTAGE      62
#define SCSCL_PRESENT_TEMPERATURE  63
#define SCSCL_MOVING               66
#define SCSCL_PRESENT_CURRENT_L    69
#define SCSCL_PRESENT_CURRENT_H    70

/* SCSCL Register addresses (EPROM - read/write) */
#define SCSCL_MIN_ANGLE_LIMIT_L    9
#define SCSCL_MIN_ANGLE_LIMIT_H    10
#define SCSCL_MAX_ANGLE_LIMIT_L    11
#define SCSCL_MAX_ANGLE_LIMIT_H    12

struct scservo_config {
	const struct device *uart_dev;
	uint8_t servo_id;
};

struct scservo_data {
	struct k_mutex lock;
	uint8_t error;
};

/**
 * @brief Send a byte via UART polling.
 */
static void scservo_uart_write_byte(const struct device *uart, uint8_t byte)
{
	uart_poll_out(uart, byte);
}

/**
 * @brief Read a byte via UART polling with timeout.
 *
 * @return 0 on success, -ETIMEDOUT on timeout.
 */
static int scservo_uart_read_byte(const struct device *uart, uint8_t *byte,
				  k_timeout_t timeout)
{
	k_timepoint_t end = sys_timepoint_calc(timeout);

	do {
		if (uart_poll_in(uart, byte) == 0) {
			return 0;
		}
		k_yield();
	} while (!sys_timepoint_expired(end));

	return -ETIMEDOUT;
}

/**
 * @brief Flush UART receive buffer.
 */
static void scservo_uart_flush_rx(const struct device *uart)
{
	uint8_t dummy;

	while (uart_poll_in(uart, &dummy) == 0) {
		/* Discard bytes */
	}
}

/**
 * @brief Wait for response header (0xFF 0xFF).
 */
static int scservo_check_header(const struct device *uart, k_timeout_t timeout)
{
	uint8_t buf[2] = {0, 0};
	uint8_t byte;
	int count = 0;

	while (count < 10) {
		int ret = scservo_uart_read_byte(uart, &byte, timeout);

		if (ret < 0) {
			return ret;
		}

		buf[1] = buf[0];
		buf[0] = byte;

		if (buf[0] == SCSERVO_HEADER_BYTE && buf[1] == SCSERVO_HEADER_BYTE) {
			return 0;
		}
		count++;
	}

	return -EIO;
}

/**
 * @brief Write command packet to servo.
 */
static void scservo_write_packet(const struct device *uart, uint8_t id,
				 uint8_t instruction, const uint8_t *params,
				 size_t param_len)
{
	uint8_t header[6];
	uint8_t msg_len = param_len + 2; /* params + instruction + checksum */

	header[0] = SCSERVO_HEADER_BYTE;
	header[1] = SCSERVO_HEADER_BYTE;
	header[2] = id;
	header[3] = msg_len;
	header[4] = instruction;

	/* Send header */
	for (int i = 0; i < 5; i++) {
		scservo_uart_write_byte(uart, header[i]);
	}

	/* Calculate checksum over ID + Length + Instruction + Params */
	uint8_t checksum = id + msg_len + instruction;

	/* Send parameters */
	for (size_t i = 0; i < param_len; i++) {
		scservo_uart_write_byte(uart, params[i]);
		checksum += params[i];
	}

	/* Send checksum */
	scservo_uart_write_byte(uart, ~checksum);
}

/**
 * @brief Read response packet from servo.
 *
 * @param uart UART device.
 * @param id Expected servo ID.
 * @param data Buffer to store received data.
 * @param len Expected data length.
 * @param error Pointer to store servo error byte.
 *
 * @return Number of data bytes received, or negative error code.
 */
static int scservo_read_packet(const struct device *uart, uint8_t id,
			       uint8_t *data, size_t len, uint8_t *error)
{
	k_timeout_t timeout = K_MSEC(CONFIG_SCSERVO_TIMEOUT_MS);
	uint8_t buf[4];
	int ret;

	/* Wait for header */
	ret = scservo_check_header(uart, timeout);
	if (ret < 0) {
		LOG_ERR("Header not found");
		return ret;
	}

	/* Read ID, Length, Error */
	for (int i = 0; i < 3; i++) {
		ret = scservo_uart_read_byte(uart, &buf[i], timeout);
		if (ret < 0) {
			LOG_ERR("Failed to read response header");
			return ret;
		}
	}

	uint8_t resp_id = buf[0];
	uint8_t resp_len = buf[1];
	uint8_t resp_err = buf[2];

	if (resp_id != id && id != SCSERVO_BROADCAST_ID) {
		LOG_ERR("ID mismatch: expected %d, got %d", id, resp_id);
		return -EIO;
	}

	if (error != NULL) {
		*error = resp_err;
	}

	/* Read data bytes */
	size_t data_len = resp_len - 2; /* Subtract error and checksum */
	uint8_t checksum = resp_id + resp_len + resp_err;

	for (size_t i = 0; i < data_len && i < len; i++) {
		ret = scservo_uart_read_byte(uart, &data[i], timeout);
		if (ret < 0) {
			LOG_ERR("Failed to read data byte %zu", i);
			return ret;
		}
		checksum += data[i];
	}

	/* Read and verify checksum */
	uint8_t recv_checksum;

	ret = scservo_uart_read_byte(uart, &recv_checksum, timeout);
	if (ret < 0) {
		LOG_ERR("Failed to read checksum");
		return ret;
	}

	if ((uint8_t)(~checksum) != recv_checksum) {
		LOG_ERR("Checksum mismatch");
		return -EIO;
	}

	return data_len;
}

/**
 * @brief Write to a servo register.
 */
static int scservo_write_reg(const struct device *dev, uint8_t addr,
			     const uint8_t *data, size_t len)
{
	const struct scservo_config *config = dev->config;
	struct scservo_data *drv_data = dev->data;
	uint8_t params[16];
	int ret;

	if (len > sizeof(params) - 1) {
		return -EINVAL;
	}

	params[0] = addr;
	memcpy(&params[1], data, len);

	k_mutex_lock(&drv_data->lock, K_FOREVER);

	scservo_uart_flush_rx(config->uart_dev);
	scservo_write_packet(config->uart_dev, config->servo_id, INST_WRITE,
			     params, len + 1);

	/* Read acknowledgment (if not broadcast) */
	if (config->servo_id != SCSERVO_BROADCAST_ID) {
		ret = scservo_read_packet(config->uart_dev, config->servo_id,
					  NULL, 0, &drv_data->error);
		if (ret < 0) {
			k_mutex_unlock(&drv_data->lock);
			return ret;
		}
	}

	k_mutex_unlock(&drv_data->lock);
	return 0;
}

/**
 * @brief Read from a servo register.
 */
static int scservo_read_reg(const struct device *dev, uint8_t addr,
			    uint8_t *data, size_t len)
{
	const struct scservo_config *config = dev->config;
	struct scservo_data *drv_data = dev->data;
	uint8_t params[2];
	int ret;

	params[0] = addr;
	params[1] = len;

	k_mutex_lock(&drv_data->lock, K_FOREVER);

	scservo_uart_flush_rx(config->uart_dev);
	scservo_write_packet(config->uart_dev, config->servo_id, INST_READ,
			     params, 2);

	ret = scservo_read_packet(config->uart_dev, config->servo_id,
				  data, len, &drv_data->error);

	k_mutex_unlock(&drv_data->lock);

	if (ret < 0) {
		return ret;
	}

	if ((size_t)ret != len) {
		return -EIO;
	}

	return 0;
}

/**
 * @brief Write a 16-bit value to a register.
 */
static int scservo_write_word(const struct device *dev, uint8_t addr,
			      uint16_t value)
{
	uint8_t data[2];

	/* SCServo uses big-endian for 16-bit values */
	data[0] = (value >> 8) & 0xFF;
	data[1] = value & 0xFF;

	return scservo_write_reg(dev, addr, data, 2);
}

/**
 * @brief Read a 16-bit value from a register.
 */
static int scservo_read_word(const struct device *dev, uint8_t addr,
			     int16_t *value)
{
	uint8_t data[2];
	int ret;

	ret = scservo_read_reg(dev, addr, data, 2);
	if (ret < 0) {
		return ret;
	}

	/* SCServo uses big-endian for 16-bit values */
	*value = ((int16_t)data[0] << 8) | data[1];

	return 0;
}

/**
 * @brief Read a byte from a register.
 */
static int scservo_read_byte(const struct device *dev, uint8_t addr,
			     uint8_t *value)
{
	return scservo_read_reg(dev, addr, value, 1);
}

/**
 * @brief Write a byte to a register.
 */
static int scservo_write_byte(const struct device *dev, uint8_t addr,
			      uint8_t value)
{
	return scservo_write_reg(dev, addr, &value, 1);
}

/* API implementations */

static int scservo_ping_impl(const struct device *dev)
{
	const struct scservo_config *config = dev->config;
	struct scservo_data *drv_data = dev->data;
	int ret;

	k_mutex_lock(&drv_data->lock, K_FOREVER);

	scservo_uart_flush_rx(config->uart_dev);
	scservo_write_packet(config->uart_dev, config->servo_id, INST_PING,
			     NULL, 0);

	ret = scservo_read_packet(config->uart_dev, config->servo_id,
				  NULL, 0, &drv_data->error);

	k_mutex_unlock(&drv_data->lock);

	return ret < 0 ? ret : 0;
}

static int scservo_write_position_impl(const struct device *dev,
				       uint16_t position, uint16_t time_ms,
				       uint16_t speed)
{
	uint8_t data[6];

	/* Position (big-endian) */
	data[0] = (position >> 8) & 0xFF;
	data[1] = position & 0xFF;
	/* Time (big-endian) */
	data[2] = (time_ms >> 8) & 0xFF;
	data[3] = time_ms & 0xFF;
	/* Speed (big-endian) */
	data[4] = (speed >> 8) & 0xFF;
	data[5] = speed & 0xFF;

	return scservo_write_reg(dev, SCSCL_GOAL_POSITION_L, data, 6);
}

static int scservo_read_position_impl(const struct device *dev,
				      int16_t *position)
{
	return scservo_read_word(dev, SCSCL_PRESENT_POSITION_L, position);
}

static int scservo_read_speed_impl(const struct device *dev, int16_t *speed)
{
	int ret;
	int16_t raw;

	ret = scservo_read_word(dev, SCSCL_PRESENT_SPEED_L, &raw);
	if (ret < 0) {
		return ret;
	}

	/* Check sign bit (bit 15) */
	if (raw & (1 << 15)) {
		*speed = -(raw & ~(1 << 15));
	} else {
		*speed = raw;
	}

	return 0;
}

static int scservo_read_load_impl(const struct device *dev, int16_t *load)
{
	int ret;
	int16_t raw;

	ret = scservo_read_word(dev, SCSCL_PRESENT_LOAD_L, &raw);
	if (ret < 0) {
		return ret;
	}

	/* Check sign bit (bit 10 for load) */
	if (raw & (1 << 10)) {
		*load = -(raw & ~(1 << 10));
	} else {
		*load = raw;
	}

	return 0;
}

static int scservo_read_voltage_impl(const struct device *dev, uint8_t *voltage)
{
	return scservo_read_byte(dev, SCSCL_PRESENT_VOLTAGE, voltage);
}

static int scservo_read_temperature_impl(const struct device *dev,
					 uint8_t *temperature)
{
	return scservo_read_byte(dev, SCSCL_PRESENT_TEMPERATURE, temperature);
}

static int scservo_read_current_impl(const struct device *dev, int16_t *current)
{
	int ret;
	int16_t raw;

	ret = scservo_read_word(dev, SCSCL_PRESENT_CURRENT_L, &raw);
	if (ret < 0) {
		return ret;
	}

	/* Check sign bit (bit 15) */
	if (raw & (1 << 15)) {
		*current = -(raw & ~(1 << 15));
	} else {
		*current = raw;
	}

	return 0;
}

static int scservo_is_moving_impl(const struct device *dev, bool *moving)
{
	uint8_t value;
	int ret;

	ret = scservo_read_byte(dev, SCSCL_MOVING, &value);
	if (ret < 0) {
		return ret;
	}

	*moving = (value != 0);
	return 0;
}

static int scservo_enable_torque_impl(const struct device *dev,
				      enum scservo_torque_mode mode)
{
	return scservo_write_byte(dev, SCSCL_TORQUE_ENABLE, (uint8_t)mode);
}

static int scservo_set_pwm_mode_impl(const struct device *dev)
{
	uint8_t data[4] = {0, 0, 0, 0};

	/* Set min and max angle limits to 0 to enter PWM mode */
	return scservo_write_reg(dev, SCSCL_MIN_ANGLE_LIMIT_L, data, 4);
}

static int scservo_write_pwm_impl(const struct device *dev, int16_t pwm)
{
	uint16_t value;

	if (pwm < 0) {
		value = (uint16_t)(-pwm) | (1 << 10);
	} else {
		value = (uint16_t)pwm;
	}

	return scservo_write_word(dev, SCSCL_GOAL_TIME_L, value);
}

static const struct scservo_driver_api scservo_api = {
	.ping = scservo_ping_impl,
	.write_position = scservo_write_position_impl,
	.read_position = scservo_read_position_impl,
	.read_speed = scservo_read_speed_impl,
	.read_load = scservo_read_load_impl,
	.read_voltage = scservo_read_voltage_impl,
	.read_temperature = scservo_read_temperature_impl,
	.read_current = scservo_read_current_impl,
	.is_moving = scservo_is_moving_impl,
	.enable_torque = scservo_enable_torque_impl,
	.set_pwm_mode = scservo_set_pwm_mode_impl,
	.write_pwm = scservo_write_pwm_impl,
};

static int scservo_init(const struct device *dev)
{
	const struct scservo_config *config = dev->config;
	struct scservo_data *data = dev->data;

	if (!device_is_ready(config->uart_dev)) {
		LOG_ERR("UART device not ready");
		return -ENODEV;
	}

	k_mutex_init(&data->lock);
	data->error = 0;

	LOG_INF("SCServo initialized (ID: %d)", config->servo_id);

	return 0;
}

#define SCSERVO_INIT(inst)                                                     \
	static const struct scservo_config scservo_config_##inst = {           \
		.uart_dev = DEVICE_DT_GET(DT_INST_BUS(inst)),                  \
		.servo_id = DT_INST_PROP(inst, servo_id),                      \
	};                                                                     \
	static struct scservo_data scservo_data_##inst;                        \
	DEVICE_DT_INST_DEFINE(inst, scservo_init, NULL,                        \
			      &scservo_data_##inst, &scservo_config_##inst,    \
			      POST_KERNEL, CONFIG_SCSERVO_INIT_PRIORITY,       \
			      &scservo_api);

DT_INST_FOREACH_STATUS_OKAY(SCSERVO_INIT)

