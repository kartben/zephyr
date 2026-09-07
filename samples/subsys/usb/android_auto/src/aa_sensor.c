/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_sensor.h"

#include <zephyr/logging/log.h>

#include "src/aa.pb.h"
#include "aa_frame.h"
#include "aa_ids.h"
#include "aa_session.h"
#include "ui.h"

LOG_MODULE_REGISTER(aa_sensor, CONFIG_SAMPLE_AA_LOG_LEVEL);

/* Sensor channel: the head unit night mode drives the LVGL theme */

int aa_sensor_start(void)
{
	SensorStartRequest req = SensorStartRequest_init_zero;
	uint8_t buf[16];
	int n;

	req.has_sensor_type = true;
	req.sensor_type = AA_SENSOR_TYPE_NIGHT_DATA;
	req.has_refresh_interval = true;
	req.refresh_interval = 0;

	n = aa_pb_encode(buf, sizeof(buf), SensorStartRequest_fields, &req);
	if (n < 0) {
		return n;
	}

	return aa_msg_send(aa_session_get()->sensor_ch, false, AA_SENSOR_START_REQUEST, buf,
			   (size_t)n);
}

void aa_sensor_handle(uint16_t msg_id, const uint8_t *body, size_t len)
{
	switch (msg_id) {
	case AA_SENSOR_START_RESPONSE: {
		SensorStartResponse rsp = SensorStartResponse_init_zero;

		if (aa_pb_decode(body, len, SensorStartResponse_fields, &rsp) == 0) {
			LOG_INF("Night mode sensor %s",
				(rsp.has_status && rsp.status != AA_STATUS_OK) ? "refused"
									       : "started");
		}
		break;
	}

	case AA_SENSOR_EVENT_INDICATION: {
		SensorEventIndication ind = SensorEventIndication_init_zero;

		if (aa_pb_decode(body, len, SensorEventIndication_fields, &ind) != 0) {
			return;
		}
		if (ind.night_mode_count > 0U) {
			LOG_INF("Night mode %s", ind.night_mode[0].is_night ? "on" : "off");
			aa_ui_set_night(ind.night_mode[0].is_night);
		}
		break;
	}

	default:
		LOG_WRN("Unhandled sensor message 0x%04x (%zu bytes)", msg_id, len);
		break;
	}
}
