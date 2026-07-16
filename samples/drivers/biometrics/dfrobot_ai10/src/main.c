/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/biometrics.h>
#include <zephyr/drivers/biometrics/dfrobot_ai10.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define AI10_NODE DT_ALIAS(ai10)

#if !DT_NODE_EXISTS(AI10_NODE)
#error "AI10 module not defined. Add 'ai10 = &<your_node>;' to your DTS aliases"
#endif

#define CAPTURE_TIMEOUT K_SECONDS(15)

static const struct device *const ai10 = DEVICE_DT_GET(AI10_NODE);

static const char *const modality_names[] = {
	[BIOMETRIC_TYPE_FINGERPRINT] = "fingerprint",
	[BIOMETRIC_TYPE_IRIS] = "iris",
	[BIOMETRIC_TYPE_FACE] = "face",
	[BIOMETRIC_TYPE_PALM] = "palm",
	[BIOMETRIC_TYPE_VOICE] = "voice",
};

static enum biometric_sensor_type uid_modality(uint16_t uid)
{
	return uid >= DFROBOT_AI10_UID_PALM_MIN ? BIOMETRIC_TYPE_PALM : BIOMETRIC_TYPE_FACE;
}

static void print_capabilities(void)
{
	struct biometric_capabilities caps;
	int ret;

	ret = biometric_get_capabilities(ai10, &caps);
	if (ret < 0) {
		LOG_ERR("Failed to get capabilities: %d", ret);
		return;
	}

	LOG_INF("Module capabilities:");
	LOG_INF("  Supported modalities:");
	for (size_t i = 0; i < ARRAY_SIZE(modality_names); i++) {
		if ((caps.supported_modalities & BIT(i)) != 0U) {
			LOG_INF("    - %s", modality_names[i]);
		}
	}
	LOG_INF("  Max templates: %u (faces: user IDs %u-%u, palms: %u-%u)", caps.max_templates,
		DFROBOT_AI10_UID_FACE_MIN, DFROBOT_AI10_UID_FACE_MAX, DFROBOT_AI10_UID_PALM_MIN,
		DFROBOT_AI10_UID_PALM_MAX);
	LOG_INF("  Samples per enrollment: %u", caps.enrollment_samples_required);
}

static int enroll(const char *prompt, uint16_t label_id, uint16_t *assigned_uid)
{
	struct biometric_capture_result capture;
	int32_t uid;
	int ret;

	LOG_INF("%s", prompt);

	ret = biometric_enroll_start(ai10, label_id);
	if (ret < 0) {
		LOG_ERR("Failed to start enrollment: %d", ret);
		return ret;
	}

	ret = biometric_enroll_capture(ai10, CAPTURE_TIMEOUT, &capture);
	if (ret < 0) {
		if (ret == -ETIMEDOUT) {
			LOG_WRN("Nothing presented before timeout");
		} else if (ret == -EEXIST) {
			LOG_WRN("Already enrolled");
		} else {
			LOG_ERR("Enrollment capture failed: %d", ret);
		}
		biometric_enroll_abort(ai10);
		return ret;
	}

	ret = biometric_enroll_finalize(ai10);
	if (ret < 0) {
		LOG_ERR("Failed to finalize enrollment: %d", ret);
		return ret;
	}

	ret = biometric_attr_get(ai10, (enum biometric_attribute)DFROBOT_AI10_ATTR_LAST_ENROLL_UID,
				 &uid);
	if (ret < 0) {
		LOG_ERR("Failed to read assigned user ID: %d", ret);
		return ret;
	}

	*assigned_uid = (uint16_t)uid;
	LOG_INF("Module assigned user ID %u (a %s template), requested ID %u was only a label",
		*assigned_uid, modality_names[capture.modality], label_id);

	return 0;
}

static void identify(void)
{
	struct biometric_match_result result;
	int ret;

	LOG_INF("Identification: show your face or palm...");

	ret = biometric_match(ai10, BIOMETRIC_MATCH_IDENTIFY, 0, CAPTURE_TIMEOUT, &result);
	if (ret == 0) {
		LOG_INF("Recognized user ID %u by %s", result.template_id,
			modality_names[result.modality]);
	} else if (ret == -ENOENT) {
		LOG_WRN("Unknown user");
	} else if (ret == -ENOTSUP) {
		LOG_WRN("A QR code was presented instead of a face or palm");
	} else if (ret == -ETIMEDOUT) {
		LOG_WRN("Nothing presented before timeout");
	} else {
		LOG_ERR("Identification failed: %d", ret);
	}
}

static void verify(uint16_t uid)
{
	int ret;

	const char *modality = modality_names[uid_modality(uid)];

	LOG_INF("Verification against user ID %u (%s): show your %s...", uid, modality, modality);

	ret = biometric_match(ai10, BIOMETRIC_MATCH_VERIFY, uid, CAPTURE_TIMEOUT, NULL);
	if (ret == 0) {
		LOG_INF("Verified user ID %u", uid);
	} else if (ret == -ENOENT) {
		LOG_WRN("Presented biometric does not match user ID %u", uid);
	} else if (ret == -ETIMEDOUT) {
		LOG_WRN("Nothing presented before timeout");
	} else {
		LOG_ERR("Verification failed: %d", ret);
	}
}

static void list_templates(void)
{
	uint16_t ids[32];
	size_t count;
	int ret;

	ret = biometric_template_list(ai10, ids, ARRAY_SIZE(ids), &count);
	if (ret < 0) {
		LOG_ERR("Failed to list templates: %d", ret);
		return;
	}

	if (count == 0) {
		LOG_INF("No templates stored");
		return;
	}

	LOG_INF("%zu stored template(s):", count);
	for (size_t i = 0; i < count; i++) {
		LOG_INF("  - user ID %u (%s)", ids[i], modality_names[uid_modality(ids[i])]);
	}
}

int main(void)
{
	uint16_t face_uid = 0;
	uint16_t palm_uid = 0;

	LOG_INF("DFRobot AI10 face and palm recognition sample");

	if (!device_is_ready(ai10)) {
		LOG_ERR("Device not ready");
		return -ENODEV;
	}

	print_capabilities();

	enroll("Enrollment 1: look at the camera to enroll your FACE...", 1, &face_uid);
	enroll("Enrollment 2: hold your PALM over the module...", 2, &palm_uid);

	list_templates();

	identify();

	if (face_uid != 0U) {
		verify(face_uid);
	}

	if (palm_uid != 0U) {
		biometric_template_delete(ai10, palm_uid);
		LOG_INF("Deleted user ID %u", palm_uid);
		list_templates();
	}

	LOG_INF("Sample complete");
	return 0;
}
