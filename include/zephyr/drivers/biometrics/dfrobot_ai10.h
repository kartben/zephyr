/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * @brief DFRobot AI10 specific biometrics attributes.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_BIOMETRICS_DFROBOT_AI10_H_
#define ZEPHYR_INCLUDE_DRIVERS_BIOMETRICS_DFROBOT_AI10_H_

#include <zephyr/drivers/biometrics.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name DFRobot AI10 user ID ranges
 *
 * The module assigns user IDs itself when a template is enrolled, using a
 * dedicated range per modality.
 *
 * @{
 */
#define DFROBOT_AI10_UID_FACE_MIN 1
#define DFROBOT_AI10_UID_FACE_MAX 1000
#define DFROBOT_AI10_UID_PALM_MIN 1001
#define DFROBOT_AI10_UID_PALM_MAX 2000
/** @} */

/**
 * @brief DFRobot AI10 specific attributes
 * @ingroup biometrics_interface
 */
enum dfrobot_ai10_attribute {
	/** User ID of the most recent successful match (read-only). */
	DFROBOT_AI10_ATTR_LAST_MATCH_UID = BIOMETRIC_ATTR_PRIV_START,
	/**
	 * User ID assigned by the module to the most recently enrolled
	 * template (read-only).
	 *
	 * The module allocates user IDs itself: 1-1000 for faces and
	 * 1001-2000 for palms. The ID requested through
	 * biometric_enroll_start() only labels the enrollment and may differ
	 * from the assigned ID.
	 */
	DFROBOT_AI10_ATTR_LAST_ENROLL_UID,
};

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_BIOMETRICS_DFROBOT_AI10_H_ */
