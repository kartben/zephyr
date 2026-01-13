/*
 * Copyright (c) 2025 Benjamin Cabé <benjamin@zephyrproject.org>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief SCServo sample application
 *
 * This sample demonstrates the FeeeTech SCServo serial bus servo driver.
 * It shows how to ping servos, move them to positions, and read feedback.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/misc/scservo/scservo.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(scservo_sample, LOG_LEVEL_INF);

/* Get servo devices from devicetree aliases */
#if DT_HAS_ALIAS(servo0)
#define SERVO0_NODE DT_ALIAS(servo0)
#else
#error "No servo0 alias defined in devicetree"
#endif

#if DT_HAS_ALIAS(servo1)
#define SERVO1_NODE DT_ALIAS(servo1)
#define HAS_SERVO1 1
#else
#define HAS_SERVO1 0
#endif

/*
 * StackChan-specific: PY32L020 IO Expander for servo power control
 * This chip controls the servo motor power rail (VM EN on GPIO 0).
 * Without enabling this, servos won't respond after a power cycle.
 */
#define PY32L020_I2C_ADDR      0x6F
#define PY32_REG_GPIO_M_L      0x03  /* GPIO Mode Low (0=input, 1=output) */
#define PY32_REG_GPIO_O_L      0x05  /* GPIO Output Low */
#define PY32_REG_GPIO_PU_L     0x09  /* GPIO Pull-Up Low */

/**
 * @brief Enable servo power on StackChan via PY32L020 IO Expander
 *
 * The PY32L020 controls the VM (motor voltage) enable pin on GPIO 0.
 * This function configures GPIO 0 as output with pull-up and sets it high.
 *
 * @param i2c I2C device for internal bus (i2c0)
 * @return 0 on success, negative errno on failure
 */
static int stackchan_enable_servo_power(const struct device *i2c)
{
	int ret;
	uint8_t reg_val;

	if (!device_is_ready(i2c)) {
		LOG_ERR("I2C device not ready");
		return -ENODEV;
	}

	/* Check if PY32L020 is present by reading a register */
	ret = i2c_reg_read_byte(i2c, PY32L020_I2C_ADDR, 0x02, &reg_val);
	if (ret != 0) {
		LOG_WRN("PY32L020 not found at 0x%02X (not StackChan?)", PY32L020_I2C_ADDR);
		return ret;
	}
	LOG_INF("PY32L020 found, version: 0x%02X", reg_val);

	/* Set GPIO 0 as output (set bit 0 in GPIO_M_L register) */
	ret = i2c_reg_read_byte(i2c, PY32L020_I2C_ADDR, PY32_REG_GPIO_M_L, &reg_val);
	if (ret == 0) {
		reg_val |= 0x01;  /* Bit 0 = GPIO 0 */
		ret = i2c_reg_write_byte(i2c, PY32L020_I2C_ADDR, PY32_REG_GPIO_M_L, reg_val);
	}
	if (ret != 0) {
		LOG_ERR("Failed to set GPIO 0 as output: %d", ret);
		return ret;
	}

	/* Enable pull-up on GPIO 0 */
	ret = i2c_reg_read_byte(i2c, PY32L020_I2C_ADDR, PY32_REG_GPIO_PU_L, &reg_val);
	if (ret == 0) {
		reg_val |= 0x01;
		ret = i2c_reg_write_byte(i2c, PY32L020_I2C_ADDR, PY32_REG_GPIO_PU_L, reg_val);
	}
	if (ret != 0) {
		LOG_ERR("Failed to enable pull-up on GPIO 0: %d", ret);
		return ret;
	}

	/* Set GPIO 0 high to enable servo power */
	ret = i2c_reg_read_byte(i2c, PY32L020_I2C_ADDR, PY32_REG_GPIO_O_L, &reg_val);
	if (ret == 0) {
		reg_val |= 0x01;
		ret = i2c_reg_write_byte(i2c, PY32L020_I2C_ADDR, PY32_REG_GPIO_O_L, reg_val);
	}
	if (ret != 0) {
		LOG_ERR("Failed to enable servo power: %d", ret);
		return ret;
	}

	LOG_INF("Servo power enabled via PY32L020");

	/* Give servos time to power up */
	k_sleep(K_MSEC(200));

	return 0;
}

int main(void)
{
	const struct device *servo0 = DEVICE_DT_GET(SERVO0_NODE);
#if HAS_SERVO1
	const struct device *servo1 = DEVICE_DT_GET(SERVO1_NODE);
#endif
	int ret;
	int16_t position;
	uint8_t voltage, temperature;

	LOG_INF("SCServo sample started");

	/*
	 * StackChan-specific: Enable servo power via PY32L020 IO Expander
	 * This must be done before servos will respond.
	 */
#if DT_NODE_EXISTS(DT_NODELABEL(i2c0))
	const struct device *i2c0 = DEVICE_DT_GET(DT_NODELABEL(i2c0));
	ret = stackchan_enable_servo_power(i2c0);
	if (ret != 0 && ret != -ENODEV) {
		LOG_ERR("Failed to enable servo power");
	}
#endif

	/* Check if servo devices are ready */
	if (!device_is_ready(servo0)) {
		LOG_ERR("Servo0 device not ready");
		return -ENODEV;
	}
	LOG_INF("Servo0 device is ready");

#if HAS_SERVO1
	if (!device_is_ready(servo1)) {
		LOG_ERR("Servo1 device not ready");
		return -ENODEV;
	}
	LOG_INF("Servo1 device is ready");
#endif

	/* Ping the servos to verify communication */
	ret = scservo_ping(servo0);
	if (ret != 0) {
		LOG_ERR("Failed to ping servo0: %d", ret);
	} else {
		LOG_INF("Servo0 ping successful");
	}

#if HAS_SERVO1
	ret = scservo_ping(servo1);
	if (ret != 0) {
		LOG_ERR("Failed to ping servo1: %d", ret);
	} else {
		LOG_INF("Servo1 ping successful");
	}
#endif

	/* Read and display servo feedback */
	ret = scservo_read_position(servo0, &position);
	if (ret == 0) {
		LOG_INF("Servo0 position: %d", position);
	}

	ret = scservo_read_voltage(servo0, &voltage);
	if (ret == 0) {
		LOG_INF("Servo0 voltage: %d.%d V", voltage / 10, voltage % 10);
	}

	ret = scservo_read_temperature(servo0, &temperature);
	if (ret == 0) {
		LOG_INF("Servo0 temperature: %d C", temperature);
	}

	/* Enable torque and move servo0 to center position */
	ret = scservo_enable_torque(servo0, SCSERVO_TORQUE_ENABLE);
	if (ret != 0) {
		LOG_ERR("Failed to enable torque: %d", ret);
	}

	LOG_INF("Moving servo0 to position 512 (center)...");
	ret = scservo_write_position(servo0, 512, 1000, 0);
	if (ret != 0) {
		LOG_ERR("Failed to move servo0: %d", ret);
	}

	k_sleep(K_MSEC(1500));

	/* Read position after move */
	ret = scservo_read_position(servo0, &position);
	if (ret == 0) {
		LOG_INF("Servo0 position after move: %d", position);
	}

	/* Demo: sweep between positions */
	LOG_INF("Starting position sweep demo...");
	for (int i = 0; i < 3; i++) {
		LOG_INF("Moving to position 400...");
		scservo_write_position(servo0, 400, 1000, 0);
		k_sleep(K_MSEC(1000));

		LOG_INF("Moving to position 600...");
		scservo_write_position(servo0, 600, 1000, 0);
		k_sleep(K_MSEC(1000));
	}

	/* Return to center */
	LOG_INF("Returning to center position...");
	scservo_write_position(servo0, 512, 1000, 0);
	k_sleep(K_MSEC(1000));

	/* Disable torque so servo can be moved by hand */
	LOG_INF("Disabling torque - you can now move the servo manually");
	scservo_enable_torque(servo0, SCSERVO_TORQUE_DISABLE);
#if HAS_SERVO1
	scservo_enable_torque(servo1, SCSERVO_TORQUE_DISABLE);
#endif

	/* Continuously read and display servo positions */
	LOG_INF("Reading servo positions (move servos by hand to see changes)...");
	while (1) {
		ret = scservo_read_position(servo0, &position);
		if (ret == 0) {
			LOG_INF("Servo0 position: %d", position);
		}

#if HAS_SERVO1
		int16_t position1;
		ret = scservo_read_position(servo1, &position1);
		if (ret == 0) {
			LOG_INF("Servo1 position: %d", position1);
		}
#endif

		k_sleep(K_MSEC(500));
	}

	return 0;
}
