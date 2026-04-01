/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SecureVault Node — biometric authentication module.
 *
 * Wraps the Zephyr 4.4 Biometrics API into a simple enroll / verify / identify
 * interface used by the rest of the application.
 */

#ifndef SECUREVAULT_AUTH_H
#define SECUREVAULT_AUTH_H

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of enrolled users. */
#define AUTH_MAX_USERS 10

/**
 * @brief Initialise the authentication subsystem.
 *
 * Must be called once before any other auth_* function.
 *
 * @return 0 on success, negative errno on failure.
 */
int auth_init(void);

/**
 * @brief Enroll a new fingerprint template.
 *
 * Blocks until the required number of captures is collected or @p timeout
 * expires. The caller should prompt the user to place / lift their finger
 * between captures.
 *
 * @param user_id   Template slot to store into (1 … AUTH_MAX_USERS).
 * @param timeout   Per-capture timeout.
 * @return 0 on success, negative errno on failure.
 */
int auth_enroll(uint16_t user_id, k_timeout_t timeout);

/**
 * @brief Verify a specific user.
 *
 * Captures one sample and checks it against the stored template for @p user_id.
 *
 * @param user_id   Template slot to verify against.
 * @param timeout   Timeout for the capture.
 * @return 0 if authenticated, -ENOENT if no match, negative errno on error.
 */
int auth_verify(uint16_t user_id, k_timeout_t timeout);

/**
 * @brief Identify — find any matching template in the database.
 *
 * Captures one sample and searches all enrolled templates.
 *
 * @param[out] matched_id  Template ID of the matched user (only valid on ret==0).
 * @param      timeout     Timeout for the capture.
 * @return 0 if a match was found, -ENOENT if none, negative errno on error.
 */
int auth_identify(uint16_t *matched_id, k_timeout_t timeout);

/**
 * @brief Delete all enrolled templates.
 * @return 0 on success.
 */
int auth_wipe(void);

/**
 * @brief Return the number of currently enrolled users.
 */
size_t auth_enrolled_count(void);

#ifdef __cplusplus
}
#endif

#endif /* SECUREVAULT_AUTH_H */
