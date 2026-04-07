/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/biometrics.h>
#include <zephyr/drivers/biometrics_dfrobot_ai10.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define BIOMETRICS_NODE DT_ALIAS(biometrics)

#if !DT_NODE_EXISTS(BIOMETRICS_NODE)
#error "Add biometrics = &dfrobot_ai10; to aliases (see README.rst)"
#endif

static const struct device *const biometric = DEVICE_DT_GET(BIOMETRICS_NODE);

static void print_capabilities(void)
{
	struct biometric_capabilities caps;
	int ret;

	ret = biometric_get_capabilities(biometric, &caps);
	if (ret < 0) {
		LOG_ERR("Failed to get capabilities: %d", ret);
		return;
	}

	LOG_INF("Sensor capabilities:");
	LOG_INF("  Type: face (module may support more)");
	LOG_INF("  Max templates (reported): %u", caps.max_templates);
	LOG_INF("  Enrollment samples: %u", caps.enrollment_samples_required);
}

static int enroll_face(uint16_t template_id)
{
	struct biometric_capabilities caps;
	struct biometric_capture_result capture = {0};
	int ret;

	LOG_INF("Enrolling face (label slot %u)", template_id);

	ret = biometric_get_capabilities(biometric, &caps);
	if (ret < 0) {
		return ret;
	}

	ret = biometric_enroll_start(biometric, template_id);
	if (ret < 0) {
		return ret;
	}

	LOG_INF("Look at the camera...");

	ret = biometric_enroll_capture(biometric, K_SECONDS(60), &capture);
	if (ret < 0) {
		biometric_enroll_abort(biometric);
		return ret;
	}

	LOG_INF("Sample %u/%u (quality %u)", capture.samples_captured,
		caps.enrollment_samples_required, capture.quality);

	ret = biometric_enroll_finalize(biometric);
	if (ret == 0) {
		LOG_INF("Enrollment complete — call list_templates() for module-assigned UIDs");
	}

	return ret;
}

static void test_verification(uint16_t template_id)
{
	struct biometric_match_result result;
	int ret;

	LOG_INF("Verify face against UID %u", template_id);

	ret = biometric_match(biometric, BIOMETRIC_MATCH_VERIFY, template_id, K_SECONDS(30),
			      &result);

	if (ret == 0) {
		LOG_INF("Match: UID %u, confidence %d, quality %u", result.template_id,
			result.confidence, result.image_quality);
	} else if (ret == -ENOENT) {
		LOG_WRN("No match");
	} else if (ret == -ETIMEDOUT) {
		LOG_WRN("Timeout");
	} else {
		LOG_ERR("Verification failed: %d", ret);
	}
}

static void test_identification(void)
{
	struct biometric_match_result result;
	int ret;

	LOG_INF("Identify (any enrolled face)...");

	ret = biometric_match(biometric, BIOMETRIC_MATCH_IDENTIFY, 0, K_SECONDS(30), &result);

	if (ret == 0) {
		LOG_INF("Matched UID %u, confidence %d, quality %u", result.template_id,
			result.confidence, result.image_quality);
	} else if (ret == -ENOENT) {
		LOG_WRN("No match");
	} else if (ret == -ETIMEDOUT) {
		LOG_WRN("Timeout");
	} else {
		LOG_ERR("Identification failed: %d", ret);
	}
}

static size_t list_templates(uint16_t *ids, size_t max_ids)
{
	size_t count = 0;
	int ret;

	ret = biometric_template_list(biometric, ids, max_ids, &count);
	if (ret < 0) {
		LOG_ERR("Failed to list templates: %d", ret);
		return 0;
	}

	if (count == 0) {
		LOG_INF("No users stored on module");
		return 0;
	}

	LOG_INF("Module user UIDs (%zu):", count);
	for (size_t i = 0; i < count; i++) {
		LOG_INF("  - UID: %u", ids[i]);
	}

	if (count > 0) {
		struct dfrobot_ai10_user_info ui;
		int uret = dfrobot_ai10_user_info_get(biometric, ids[0], &ui);

		if (uret == 0) {
			LOG_INF("GET_USER_INFO for UID %u: name=\"%s\", admin=%d", ui.uid,
				ui.user_name, ui.is_admin);
		} else {
			LOG_WRN("dfrobot_ai10_user_info_get failed: %d", uret);
		}
	}

	return count;
}

int main(void)
{
	uint16_t ids[50];
	size_t n;
	uint16_t uid;

	LOG_INF("DFRobot AI10 face sample (UART 115200 8N1)");

	if (!device_is_ready(biometric)) {
		LOG_ERR("Biometric device not ready");
		return -ENODEV;
	}

	print_capabilities();

	enroll_face(1);
	n = list_templates(ids, ARRAY_SIZE(ids));
	uid = (n > 0) ? ids[0] : 1U;

	test_verification(uid);
	test_identification();

	if (n > 0) {
		biometric_template_delete(biometric, uid);
	}
	list_templates(ids, ARRAY_SIZE(ids));

	LOG_INF("Test complete");
	return 0;
}
