/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup feetech_scs_interface
 * @brief Public API for Feetech SCS serial bus servos.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MISC_FEETECH_SCS_FEETECH_SCS_H_
#define ZEPHYR_INCLUDE_DRIVERS_MISC_FEETECH_SCS_FEETECH_SCS_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup feetech_scs_interface Feetech SCS servo
 * @ingroup misc_interfaces
 * @brief Feetech SCS series serial bus servos.
 * @since 4.5
 * @version 0.1.0
 *
 * Each servo is a device of its own. Servos sharing a bus are accessed one command at a time, so
 * the functions below may be called from several threads.
 *
 * Positions and speeds are in the servo's raw units. For the SCS0009, a position step is about
 * 0.3 degrees.
 * @{
 */

/** Maximum position accepted by feetech_scs_set_position(). */
#define FEETECH_SCS_POSITION_MAX 1023

/** Maximum magnitude of the duty cycle accepted by feetech_scs_set_pwm(). */
#define FEETECH_SCS_PWM_MAX 1000

/** @brief Servo operating mode. */
enum feetech_scs_mode {
	/** Move to and hold the position set with feetech_scs_set_position(). */
	FEETECH_SCS_MODE_POSITION,
	/** Rotate continuously at the duty cycle set with feetech_scs_set_pwm(). */
	FEETECH_SCS_MODE_PWM,
};

/** @brief Servo feedback, as read by feetech_scs_get_status(). */
struct feetech_scs_status {
	/** Current position. */
	uint16_t position;
	/** Current speed, negative when the position decreases. */
	int16_t speed;
	/** Motor load in 0.1 % of the maximum, negative when driving towards lower positions. */
	int16_t load;
	/** Current consumption in raw units. */
	int16_t current;
	/** Supply voltage in units of 0.1 V. */
	uint8_t voltage;
	/** Internal temperature in degrees Celsius. */
	uint8_t temperature;
	/** True while the servo is moving to its goal position. */
	bool moving;
};

/**
 * @brief Check that a servo answers on its bus.
 *
 * @param dev Servo device.
 *
 * @retval 0 The servo answered.
 * @retval -ETIMEDOUT The servo did not answer.
 * @retval -EIO Malformed answer.
 */
int feetech_scs_ping(const struct device *dev);

/**
 * @brief Enable or disable the servo motor torque.
 *
 * With torque disabled the servo can be moved by hand and still reports its position.
 *
 * @param dev Servo device.
 * @param enable True to enable torque.
 *
 * @retval 0 Success.
 * @retval -ETIMEDOUT The servo did not answer.
 * @retval -EIO Malformed answer.
 */
int feetech_scs_set_torque(const struct device *dev, bool enable);

/**
 * @brief Move the servo to a position.
 *
 * The servo reaches @p position in @p time_ms milliseconds, or at @p speed if @p time_ms is 0, or
 * as fast as it can if both are 0. The servo must have its torque enabled to move.
 *
 * @param dev Servo device.
 * @param position Goal position, up to @ref FEETECH_SCS_POSITION_MAX.
 * @param time_ms Time to reach the goal position, in milliseconds.
 * @param speed Speed to reach the goal position with.
 *
 * @retval 0 Success.
 * @retval -EINVAL @p position is out of range.
 * @retval -EPERM The servo is not in ::FEETECH_SCS_MODE_POSITION.
 * @retval -ETIMEDOUT The servo did not answer.
 * @retval -EIO Malformed answer.
 */
int feetech_scs_set_position(const struct device *dev, uint16_t position, uint16_t time_ms,
			     uint16_t speed);

/**
 * @brief Read the current position of the servo.
 *
 * @param dev Servo device.
 * @param[out] position Current position.
 *
 * @retval 0 Success.
 * @retval -ETIMEDOUT The servo did not answer.
 * @retval -EIO Malformed answer.
 */
int feetech_scs_get_position(const struct device *dev, uint16_t *position);

/**
 * @brief Read the servo feedback registers in a single transaction.
 *
 * @param dev Servo device.
 * @param[out] status Servo feedback.
 *
 * @retval 0 Success.
 * @retval -ETIMEDOUT The servo did not answer.
 * @retval -EIO Malformed answer.
 */
int feetech_scs_get_status(const struct device *dev, struct feetech_scs_status *status);

/**
 * @brief Select the servo operating mode.
 *
 * The PWM mode is entered by clearing the servo angle limits. The limits are saved by the driver
 * and restored when switching back to ::FEETECH_SCS_MODE_POSITION. The change is not written to
 * the servo non-volatile memory, so the servo is back in position mode after a power cycle. The
 * driver assumes the servo is in position mode when the system starts.
 *
 * @param dev Servo device.
 * @param mode Operating mode.
 *
 * @retval 0 Success.
 * @retval -EINVAL @p mode is invalid.
 * @retval -ETIMEDOUT The servo did not answer.
 * @retval -EIO Malformed answer.
 */
int feetech_scs_set_mode(const struct device *dev, enum feetech_scs_mode mode);

/**
 * @brief Set the motor duty cycle in PWM mode.
 *
 * @param dev Servo device.
 * @param duty Duty cycle in 0.1 %, from -@ref FEETECH_SCS_PWM_MAX to @ref FEETECH_SCS_PWM_MAX.
 *             The sign selects the direction of rotation.
 *
 * @retval 0 Success.
 * @retval -EINVAL @p duty is out of range.
 * @retval -EPERM The servo is not in ::FEETECH_SCS_MODE_PWM.
 * @retval -ETIMEDOUT The servo did not answer.
 * @retval -EIO Malformed answer.
 */
int feetech_scs_set_pwm(const struct device *dev, int16_t duty);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_MISC_FEETECH_SCS_FEETECH_SCS_H_ */
