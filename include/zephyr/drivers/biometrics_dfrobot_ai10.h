/*
 * Copyright (c) 2026 The Zephyr Project contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief DFRobot AI10 (SEN0677) face module — driver-specific API.
 *
 * User name and admin role are read with @ref dfrobot_ai10_user_info_get (vendor
 * command GET_USER_INFO by UID). Setting admin or display name applies only to the
 * next enrollment: use @ref dfrobot_ai10_enroll_options_set before
 * biometric_enroll_start / biometric_enroll_capture, or @ref dfrobot_ai10_enroll_options_clear
 * to restore defaults.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_BIOMETRICS_DFROBOT_AI10_H_
#define ZEPHYR_INCLUDE_DRIVERS_BIOMETRICS_DFROBOT_AI10_H_

#include <zephyr/device.h>
#include <zephyr/drivers/biometrics.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** User record returned by @ref dfrobot_ai10_user_info_get. */
struct dfrobot_ai10_user_info {
	uint16_t uid;
	/** UTF-8 or ASCII name from the module (up to 32 bytes; NUL-terminated if shorter). */
	char user_name[32];
	bool is_admin;
};

/** Optional overrides for the next @ref biometric_enroll_capture only. */
struct dfrobot_ai10_enroll_options {
	/** When true, @ref is_admin and @ref user_name are applied; when false, options are cleared. */
	bool use_custom;
	/** Maps to vendor admin vs normal user (only if @ref use_custom). */
	bool is_admin;
	/**
	 * Display name (only if @ref use_custom). If the first byte is NUL, the driver uses
	 * the default label @c u%04u with the enrollment template ID.
	 */
	char user_name[32];
};

/**
 * @brief Identity of this driver's biometric API (for @ref dfrobot_ai10_is_ready).
 */
extern const struct biometric_driver_api biometrics_dfrobot_ai10_api;

/**
 * @brief True if @a dev is ready and bound to the DFRobot AI10 driver.
 */
static inline bool dfrobot_ai10_is_ready(const struct device *dev)
{
	return (dev != NULL) && device_is_ready(dev) && DEVICE_API_IS(biometric, dev) &&
	       (dev->api == &biometrics_dfrobot_ai10_api);
}

/**
 * @brief Read user name and admin flag for a stored UID.
 *
 * @param dev DFRobot AI10 biometric device.
 * @param uid User ID on the module.
 * @param info Output; must not be NULL.
 *
 * @retval 0 Success.
 * @retval -EINVAL Invalid argument.
 * @retval -ENODEV Device not ready.
 * @retval -ENOTSUP @a dev is not this driver.
 * @retval -EBADMSG Malformed reply.
 * @retval Other Negative errno from the protocol layer.
 */
int dfrobot_ai10_user_info_get(const struct device *dev, uint16_t uid,
			       struct dfrobot_ai10_user_info *info);

/**
 * @brief Set enrollment overrides for the next capture on this device.
 *
 * @param dev DFRobot AI10 biometric device.
 * @param opts Options; must not be NULL. If @c use_custom is false, stored overrides are cleared.
 *
 * @retval 0 Success.
 * @retval -EINVAL Invalid argument.
 * @retval -ENOTSUP @a dev is not this driver.
 */
int dfrobot_ai10_enroll_options_set(const struct device *dev,
				    const struct dfrobot_ai10_enroll_options *opts);

/**
 * @brief Clear enrollment overrides (default admin + automatic @c u%04u name).
 *
 * @param dev DFRobot AI10 biometric device.
 *
 * @retval 0 Success.
 * @retval -ENOTSUP @a dev is not this driver.
 */
int dfrobot_ai10_enroll_options_clear(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_BIOMETRICS_DFROBOT_AI10_H_ */
