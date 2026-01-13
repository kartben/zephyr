/*
 * Copyright (c) 2026 Your Name
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public API for FeeeTech SCServo serial bus servos.
 * @ingroup scservo
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MISC_SCSERVO_SCSERVO_H_
#define ZEPHYR_INCLUDE_DRIVERS_MISC_SCSERVO_SCSERVO_H_

#include <zephyr/device.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup scservo SCServo Serial Bus Servo
 * @ingroup misc_interfaces
 * @since 4.2
 * @version 0.1.0
 * @{
 */

/**
 * @brief Torque enable modes.
 */
enum scservo_torque_mode {
	SCSERVO_TORQUE_DISABLE = 0, /**< Torque disabled, servo can be moved freely. */
	SCSERVO_TORQUE_ENABLE = 1,  /**< Torque enabled, servo holds position. */
	SCSERVO_TORQUE_DAMPING = 2, /**< Damping mode, servo provides resistance. */
};

/**
 * @cond INTERNAL_HIDDEN
 *
 * SCServo driver API.
 */
__subsystem struct scservo_driver_api {
	int (*ping)(const struct device *dev);
	int (*write_position)(const struct device *dev, uint16_t position,
			      uint16_t time_ms, uint16_t speed);
	int (*read_position)(const struct device *dev, int16_t *position);
	int (*read_speed)(const struct device *dev, int16_t *speed);
	int (*read_load)(const struct device *dev, int16_t *load);
	int (*read_voltage)(const struct device *dev, uint8_t *voltage);
	int (*read_temperature)(const struct device *dev, uint8_t *temperature);
	int (*read_current)(const struct device *dev, int16_t *current);
	int (*is_moving)(const struct device *dev, bool *moving);
	int (*enable_torque)(const struct device *dev, enum scservo_torque_mode mode);
	int (*set_pwm_mode)(const struct device *dev);
	int (*write_pwm)(const struct device *dev, int16_t pwm);
};

/**
 * @endcond
 */

/**
 * @brief Ping the servo to check if it responds.
 *
 * Send a ping command to verify communication with the servo.
 *
 * @param dev Pointer to the SCServo device instance.
 *
 * @retval 0 Servo responded successfully.
 * @retval -ETIMEDOUT No response received within timeout.
 * @retval -EIO Communication error or checksum mismatch.
 */
__syscall int scservo_ping(const struct device *dev);

static inline int z_impl_scservo_ping(const struct device *dev)
{
	const struct scservo_driver_api *api =
		(const struct scservo_driver_api *)dev->api;

	if (api->ping == NULL) {
		return -ENOSYS;
	}

	return api->ping(dev);
}

/**
 * @brief Move the servo to a target position.
 *
 * Command the servo to move to the specified position. The movement is
 * controlled by either time or speed, depending on which is non-zero.
 *
 * @param dev Pointer to the SCServo device instance.
 * @param position Target position in servo units (0-1023 for most servos).
 *                 Mid-position is typically 512.
 * @param time_ms Time to complete the movement, in milliseconds.
 *                Set to 0 to use speed control instead.
 * @param speed Movement speed in servo units per second.
 *              Set to 0 to use time control instead.
 *
 * @retval 0 Command sent successfully.
 * @retval -ETIMEDOUT No acknowledgment received within timeout.
 * @retval -EIO Communication error.
 */
__syscall int scservo_write_position(const struct device *dev,
				     uint16_t position, uint16_t time_ms,
				     uint16_t speed);

static inline int z_impl_scservo_write_position(const struct device *dev,
						uint16_t position,
						uint16_t time_ms,
						uint16_t speed)
{
	const struct scservo_driver_api *api =
		(const struct scservo_driver_api *)dev->api;

	if (api->write_position == NULL) {
		return -ENOSYS;
	}

	return api->write_position(dev, position, time_ms, speed);
}

/**
 * @brief Read the current position of the servo.
 *
 * @param dev Pointer to the SCServo device instance.
 * @param[out] position Pointer to store the current position (0-1023).
 *                      Must not be NULL.
 *
 * @retval 0 Position read successfully.
 * @retval -ETIMEDOUT No response received within timeout.
 * @retval -EIO Communication error.
 */
__syscall int scservo_read_position(const struct device *dev,
				    int16_t *position);

static inline int z_impl_scservo_read_position(const struct device *dev,
					       int16_t *position)
{
	const struct scservo_driver_api *api =
		(const struct scservo_driver_api *)dev->api;

	if (api->read_position == NULL) {
		return -ENOSYS;
	}

	return api->read_position(dev, position);
}

/**
 * @brief Read the current speed of the servo.
 *
 * @param dev Pointer to the SCServo device instance.
 * @param[out] speed Pointer to store the current speed. Positive values
 *                   indicate clockwise rotation. Must not be NULL.
 *
 * @retval 0 Speed read successfully.
 * @retval -ETIMEDOUT No response received within timeout.
 * @retval -EIO Communication error.
 */
__syscall int scservo_read_speed(const struct device *dev, int16_t *speed);

static inline int z_impl_scservo_read_speed(const struct device *dev,
					    int16_t *speed)
{
	const struct scservo_driver_api *api =
		(const struct scservo_driver_api *)dev->api;

	if (api->read_speed == NULL) {
		return -ENOSYS;
	}

	return api->read_speed(dev, speed);
}

/**
 * @brief Read the current load (torque) of the servo.
 *
 * The load value represents the percentage of maximum torque being applied.
 *
 * @param dev Pointer to the SCServo device instance.
 * @param[out] load Pointer to store the load value (-1000 to 1000).
 *                  Positive values indicate clockwise load. Must not be NULL.
 *
 * @retval 0 Load read successfully.
 * @retval -ETIMEDOUT No response received within timeout.
 * @retval -EIO Communication error.
 */
__syscall int scservo_read_load(const struct device *dev, int16_t *load);

static inline int z_impl_scservo_read_load(const struct device *dev,
					   int16_t *load)
{
	const struct scservo_driver_api *api =
		(const struct scservo_driver_api *)dev->api;

	if (api->read_load == NULL) {
		return -ENOSYS;
	}

	return api->read_load(dev, load);
}

/**
 * @brief Read the supply voltage of the servo.
 *
 * @param dev Pointer to the SCServo device instance.
 * @param[out] voltage Pointer to store the voltage in units of 0.1 V.
 *                     For example, 70 means 7.0 V. Must not be NULL.
 *
 * @retval 0 Voltage read successfully.
 * @retval -ETIMEDOUT No response received within timeout.
 * @retval -EIO Communication error.
 */
__syscall int scservo_read_voltage(const struct device *dev, uint8_t *voltage);

static inline int z_impl_scservo_read_voltage(const struct device *dev,
					      uint8_t *voltage)
{
	const struct scservo_driver_api *api =
		(const struct scservo_driver_api *)dev->api;

	if (api->read_voltage == NULL) {
		return -ENOSYS;
	}

	return api->read_voltage(dev, voltage);
}

/**
 * @brief Read the internal temperature of the servo.
 *
 * @param dev Pointer to the SCServo device instance.
 * @param[out] temperature Pointer to store the temperature in degrees Celsius.
 *                         Must not be NULL.
 *
 * @retval 0 Temperature read successfully.
 * @retval -ETIMEDOUT No response received within timeout.
 * @retval -EIO Communication error.
 */
__syscall int scservo_read_temperature(const struct device *dev,
				       uint8_t *temperature);

static inline int z_impl_scservo_read_temperature(const struct device *dev,
						  uint8_t *temperature)
{
	const struct scservo_driver_api *api =
		(const struct scservo_driver_api *)dev->api;

	if (api->read_temperature == NULL) {
		return -ENOSYS;
	}

	return api->read_temperature(dev, temperature);
}

/**
 * @brief Read the motor current of the servo.
 *
 * @param dev Pointer to the SCServo device instance.
 * @param[out] current Pointer to store the current in milliamps.
 *                     Positive values indicate motor is driving.
 *                     Must not be NULL.
 *
 * @retval 0 Current read successfully.
 * @retval -ETIMEDOUT No response received within timeout.
 * @retval -EIO Communication error.
 */
__syscall int scservo_read_current(const struct device *dev, int16_t *current);

static inline int z_impl_scservo_read_current(const struct device *dev,
					      int16_t *current)
{
	const struct scservo_driver_api *api =
		(const struct scservo_driver_api *)dev->api;

	if (api->read_current == NULL) {
		return -ENOSYS;
	}

	return api->read_current(dev, current);
}

/**
 * @brief Check if the servo is currently moving.
 *
 * @param dev Pointer to the SCServo device instance.
 * @param[out] moving Pointer to store the moving status.
 *                    True if servo is in motion. Must not be NULL.
 *
 * @retval 0 Status read successfully.
 * @retval -ETIMEDOUT No response received within timeout.
 * @retval -EIO Communication error.
 */
__syscall int scservo_is_moving(const struct device *dev, bool *moving);

static inline int z_impl_scservo_is_moving(const struct device *dev,
					   bool *moving)
{
	const struct scservo_driver_api *api =
		(const struct scservo_driver_api *)dev->api;

	if (api->is_moving == NULL) {
		return -ENOSYS;
	}

	return api->is_moving(dev, moving);
}

/**
 * @brief Enable or disable servo torque.
 *
 * @param dev Pointer to the SCServo device instance.
 * @param mode Torque mode to set. See @ref scservo_torque_mode.
 *
 * @retval 0 Torque mode set successfully.
 * @retval -ETIMEDOUT No acknowledgment received within timeout.
 * @retval -EIO Communication error.
 */
__syscall int scservo_enable_torque(const struct device *dev,
				    enum scservo_torque_mode mode);

static inline int z_impl_scservo_enable_torque(const struct device *dev,
					       enum scservo_torque_mode mode)
{
	const struct scservo_driver_api *api =
		(const struct scservo_driver_api *)dev->api;

	if (api->enable_torque == NULL) {
		return -ENOSYS;
	}

	return api->enable_torque(dev, mode);
}

/**
 * @brief Switch the servo to PWM (wheel) mode.
 *
 * In PWM mode, the servo operates as a continuous rotation motor
 * controlled by PWM duty cycle instead of position.
 *
 * @param dev Pointer to the SCServo device instance.
 *
 * @retval 0 Mode switched successfully.
 * @retval -ETIMEDOUT No acknowledgment received within timeout.
 * @retval -EIO Communication error.
 */
__syscall int scservo_set_pwm_mode(const struct device *dev);

static inline int z_impl_scservo_set_pwm_mode(const struct device *dev)
{
	const struct scservo_driver_api *api =
		(const struct scservo_driver_api *)dev->api;

	if (api->set_pwm_mode == NULL) {
		return -ENOSYS;
	}

	return api->set_pwm_mode(dev);
}

/**
 * @brief Set PWM output in wheel mode.
 *
 * Control the servo motor speed and direction when in PWM mode.
 *
 * @param dev Pointer to the SCServo device instance.
 * @param pwm PWM duty cycle (-1000 to 1000). Positive values rotate
 *            clockwise, negative values rotate counter-clockwise.
 *            Magnitude determines speed.
 *
 * @retval 0 PWM set successfully.
 * @retval -ETIMEDOUT No acknowledgment received within timeout.
 * @retval -EIO Communication error.
 */
__syscall int scservo_write_pwm(const struct device *dev, int16_t pwm);

static inline int z_impl_scservo_write_pwm(const struct device *dev,
					   int16_t pwm)
{
	const struct scservo_driver_api *api =
		(const struct scservo_driver_api *)dev->api;

	if (api->write_pwm == NULL) {
		return -ENOSYS;
	}

	return api->write_pwm(dev, pwm);
}

/** @} */

#ifdef __cplusplus
}
#endif

#include <zephyr/syscalls/scservo.h>

#endif /* ZEPHYR_INCLUDE_DRIVERS_MISC_SCSERVO_SCSERVO_H_ */
