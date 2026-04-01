/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SecureVault Node — biometric authentication module
 *
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │  Zephyr 4.4 features showcased in this file                            │
 * │                                                                         │
 * │  1. Biometrics API  (include/zephyr/drivers/biometrics.h)              │
 * │     Brand-new driver subsystem for fingerprint / iris / face sensors.  │
 * │     Uniform enroll → capture → finalize workflow across all backends.  │
 * │                                                                         │
 * │  2. Scope-based Cleanup  (include/zephyr/cleanup/kernel.h)             │
 * │     RAII-style resource management in C using __cleanup__ attribute.   │
 * │     scope_guard(k_mutex)  — auto-unlock on any return path.            │
 * │     scope_defer(biometric_enroll_abort) — auto-abort if enroll fails.  │
 * │     Eliminates the error-path goto ladder that embedded C is notorious  │
 * │     for. See "Before / After" comment below for a direct comparison.   │
 * └─────────────────────────────────────────────────────────────────────────┘
 */

#include "auth.h"

#include <zephyr/device.h>
#include <zephyr/drivers/biometrics.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/*
 * [4.4 NEW] Scope-based cleanup — import the pre-defined kernel helpers.
 * This single include gives us: scope_guard(k_mutex), scope_defer(k_free),
 * scope_defer(k_mutex_unlock), scope_defer(k_sem_give), and more.
 */
#include <zephyr/cleanup/kernel.h>

LOG_MODULE_REGISTER(auth, LOG_LEVEL_INF);

/* -------------------------------------------------------------------------
 * Device binding — resolved at boot via DT alias "fingerprint".
 * On native_sim the alias points to the biometrics emulator node added in
 * boards/native_sim.overlay.  On real hardware swap the overlay for a node
 * targeting, e.g., the ZFM-x0 optical sensor or GT5x capacitive sensor.
 * ---------------------------------------------------------------------- */
#define FINGERPRINT_NODE DT_ALIAS(fingerprint)

#if !DT_NODE_EXISTS(FINGERPRINT_NODE)
#error "No 'fingerprint' DT alias found. Add one to your board overlay."
#endif

static const struct device *const fp = DEVICE_DT_GET(FINGERPRINT_NODE);

/*
 * Mutex that protects access to the sensor during multi-step operations
 * (enroll, verify).  Demonstrated with scope_guard below.
 */
static struct k_mutex sensor_mutex;

/* -------------------------------------------------------------------------
 * [4.4 NEW] Custom scope-defer definition for biometric_enroll_abort.
 *
 * SCOPE_DEFER_DEFINE teaches the cleanup framework how to call a function
 * with a specific argument type.  After this line we can write:
 *
 *   scope_defer(biometric_enroll_abort)(fp);
 *
 * …and the compiler guarantees biometric_enroll_abort(fp) will run when
 * the enclosing block exits, whether by return, goto, or fall-through.
 * ---------------------------------------------------------------------- */
SCOPE_DEFER_DEFINE(biometric_enroll_abort, const struct device *);

/* -------------------------------------------------------------------------
 * auth_init
 * ---------------------------------------------------------------------- */
int auth_init(void)
{
	if (!device_is_ready(fp)) {
		LOG_ERR("Fingerprint sensor not ready");
		return -ENODEV;
	}

	k_mutex_init(&sensor_mutex);

	/* Tune the sensor — security level 7 = fewer false accepts. */
	biometric_attr_set(fp, BIOMETRIC_ATTR_SECURITY_LEVEL, 7);

	struct biometric_capabilities caps;

	biometric_get_capabilities(fp, &caps);
	LOG_INF("Fingerprint sensor ready — max %u templates, %u samples/enroll",
		caps.max_templates, caps.enrollment_samples_required);

	return 0;
}

/* -------------------------------------------------------------------------
 * auth_enroll
 *
 * SCOPE CLEANUP SHOWCASE
 * ──────────────────────
 *
 * Before 4.4 (classic goto pattern — hard to read, easy to get wrong):
 *
 *   int auth_enroll_old(uint16_t user_id) {
 *       k_mutex_lock(&sensor_mutex, K_FOREVER);
 *       biometric_enroll_start(fp, user_id);
 *
 *       for (int i = 0; i < required; i++) {
 *           ret = biometric_enroll_capture(fp, K_SECONDS(5), &cap);
 *           if (ret < 0) goto abort;      // must remember to unlock too
 *       }
 *       ret = biometric_enroll_finalize(fp);
 *       goto done;
 *   abort:
 *       biometric_enroll_abort(fp);
 *   done:
 *       k_mutex_unlock(&sensor_mutex);   // easy to forget on every path
 *       return ret;
 *   }
 *
 * After 4.4 (scope cleanup — linear, declarative, compiler-enforced):
 * ---------------------------------------------------------------------- */
int auth_enroll(uint16_t user_id, k_timeout_t timeout)
{
	int ret;

	/*
	 * [4.4 NEW] scope_guard(k_mutex) acquires the mutex NOW and releases
	 * it automatically when this function returns — on any path.
	 */
	scope_guard(k_mutex)(&sensor_mutex);

	biometric_led_control(fp, BIOMETRIC_LED_BREATHE);

	ret = biometric_enroll_start(fp, user_id);
	if (ret < 0) {
		LOG_ERR("enroll_start failed: %d", ret);
		biometric_led_control(fp, BIOMETRIC_LED_OFF);
		return ret;
	}

	/*
	 * [4.4 NEW] scope_defer(biometric_enroll_abort) registers an abort
	 * call that fires if we return before finalize.  After a successful
	 * finalize the sensor resets its own state so the extra abort is
	 * a no-op (returns -EALREADY which we ignore here).
	 *
	 * Without this, every early-return path above would need its own
	 * manual biometric_enroll_abort() — classic C error-path footgun.
	 */
	scope_defer(biometric_enroll_abort)(fp);

	struct biometric_capabilities caps;

	biometric_get_capabilities(fp, &caps);

	struct biometric_capture_result cap = {0};

	do {
		LOG_INF("Place finger… (%u/%u)", cap.samples_captured + 1,
			caps.enrollment_samples_required);

		ret = biometric_enroll_capture(fp, timeout, &cap);
		if (ret < 0) {
			LOG_ERR("Capture failed: %d", ret);
			biometric_led_control(fp, BIOMETRIC_LED_BLINK);
			biometric_led_control(fp, BIOMETRIC_LED_OFF);
			return ret; /* scope_defer fires here → abort called */
		}

		LOG_INF("  Sample %u/%u — quality %u%%", cap.samples_captured,
			caps.enrollment_samples_required, cap.quality);

		if (cap.samples_captured < caps.enrollment_samples_required) {
			LOG_INF("Lift finger and place again…");
			k_sleep(K_MSEC(400));
		}

	} while (cap.samples_captured < caps.enrollment_samples_required);

	ret = biometric_enroll_finalize(fp);
	if (ret == 0) {
		LOG_INF("Enrollment complete for user %u", user_id);
		biometric_led_control(fp, BIOMETRIC_LED_ON);
		k_sleep(K_MSEC(500));
	} else {
		LOG_ERR("Finalize failed: %d", ret);
	}

	biometric_led_control(fp, BIOMETRIC_LED_OFF);

	/*
	 * scope_guard(k_mutex) releases the lock.
	 * scope_defer(biometric_enroll_abort) fires (no-op after finalize).
	 */
	return ret;
}

/* -------------------------------------------------------------------------
 * auth_verify
 * ---------------------------------------------------------------------- */
int auth_verify(uint16_t user_id, k_timeout_t timeout)
{
	struct biometric_match_result result = {0};

	/* scope_guard ensures the mutex is released even on -ETIMEDOUT */
	scope_guard(k_mutex)(&sensor_mutex);

	biometric_led_control(fp, BIOMETRIC_LED_BLINK);

	int ret = biometric_match(fp, BIOMETRIC_MATCH_VERIFY, user_id, timeout, &result);

	biometric_led_control(fp, BIOMETRIC_LED_OFF);

	if (ret == 0) {
		LOG_INF("Authenticated: user %u (confidence %d, quality %u%%)",
			result.template_id, result.confidence, result.image_quality);
	} else if (ret == -ENOENT) {
		LOG_WRN("Fingerprint does not match user %u", user_id);
	} else if (ret == -ETIMEDOUT) {
		LOG_WRN("No finger detected within timeout");
	} else {
		LOG_ERR("Match error: %d", ret);
	}

	return ret;
}

/* -------------------------------------------------------------------------
 * auth_identify
 * ---------------------------------------------------------------------- */
int auth_identify(uint16_t *matched_id, k_timeout_t timeout)
{
	struct biometric_match_result result = {0};

	scope_guard(k_mutex)(&sensor_mutex);

	biometric_led_control(fp, BIOMETRIC_LED_BLINK);

	int ret = biometric_match(fp, BIOMETRIC_MATCH_IDENTIFY, 0, timeout, &result);

	biometric_led_control(fp, BIOMETRIC_LED_OFF);

	if (ret == 0) {
		*matched_id = result.template_id;
		LOG_INF("Identified: user %u (confidence %d)", result.template_id,
			result.confidence);
	} else if (ret == -ENOENT) {
		LOG_WRN("Unknown fingerprint — access denied");
	}

	return ret;
}

/* -------------------------------------------------------------------------
 * auth_wipe
 * ---------------------------------------------------------------------- */
int auth_wipe(void)
{
	scope_guard(k_mutex)(&sensor_mutex);

	int ret = biometric_template_delete_all(fp);

	if (ret == 0) {
		LOG_INF("All templates wiped");
	}
	return ret;
}

/* -------------------------------------------------------------------------
 * auth_enrolled_count
 * ---------------------------------------------------------------------- */
size_t auth_enrolled_count(void)
{
	uint16_t ids[AUTH_MAX_USERS];
	size_t count = 0;

	scope_guard(k_mutex)(&sensor_mutex);
	biometric_template_list(fp, ids, ARRAY_SIZE(ids), &count);

	return count;
}
