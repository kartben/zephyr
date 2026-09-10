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

LOG_MODULE_REGISTER(aa_sensor, CONFIG_SAMPLE_AA_HU_LOG_LEVEL);

/*
 * Sensor channel. A phone refuses to project without a driving status source,
 * and restricts what it will show unless the vehicle is standing still, so the
 * head unit reports a parked, braked vehicle once and leaves it at that; a real
 * one would follow the vehicle bus. Night mode is the exception: it is sent
 * again whenever the light level changes.
 */

static bool night_started;
static bool is_night;

static int send_event(int32_t sensor_type)
{
	SensorEventIndication ind = SensorEventIndication_init_zero;
	uint8_t buf[32];
	int len;

	switch (sensor_type) {
	case AA_SENSOR_TYPE_DRIVING_STATUS:
		ind.driving_status_count = 1;
		ind.driving_status[0].has_status = true;
		ind.driving_status[0].status = AA_DRIVING_STATUS_UNRESTRICTED;
		break;
	case AA_SENSOR_TYPE_NIGHT_DATA:
		ind.night_mode_count = 1;
		ind.night_mode[0].has_is_night = true;
		ind.night_mode[0].is_night = is_night;
		break;
	case AA_SENSOR_TYPE_PARKING_BRAKE:
		ind.parking_brake_count = 1;
		ind.parking_brake[0].has_is_engaged = true;
		ind.parking_brake[0].is_engaged = true;
		break;
	case AA_SENSOR_TYPE_GEAR:
		ind.gear_count = 1;
		ind.gear[0].has_gear = true;
		ind.gear[0].gear = AA_GEAR_PARK;
		break;
	default:
		return 0;
	}

	len = aa_pb_encode(buf, sizeof(buf), SensorEventIndication_fields, &ind);
	if (len < 0) {
		return len;
	}

	return aa_msg_send(aa_hu_session_get()->sensor_ch, false, AA_SENSOR_EVENT_INDICATION, buf,
			   (size_t)len);
}

void aa_sensor_handle(uint16_t msg_id, const uint8_t *body, size_t len)
{
	SensorStartRequest req = SensorStartRequest_init_zero;
	SensorStartResponse rsp = SensorStartResponse_init_zero;
	uint8_t buf[16];
	int n;

	if (msg_id != AA_SENSOR_START_REQUEST) {
		LOG_DBG("Unhandled sensor message 0x%04x", msg_id);
		return;
	}

	if (aa_pb_decode(body, len, SensorStartRequest_fields, &req) != 0) {
		return;
	}

	LOG_INF("Sensor %d started", req.sensor_type);

	if (req.sensor_type == AA_SENSOR_TYPE_NIGHT_DATA) {
		night_started = true;
	}

	rsp.has_status = true;
	rsp.status = 0;
	n = aa_pb_encode(buf, sizeof(buf), SensorStartResponse_fields, &rsp);
	if (n < 0) {
		return;
	}

	if (aa_msg_send(aa_hu_session_get()->sensor_ch, false, AA_SENSOR_START_RESPONSE, buf,
			(size_t)n) != 0) {
		return;
	}

	(void)send_event(req.sensor_type);
}

void aa_sensor_set_night(bool night)
{
	if (night == is_night) {
		return;
	}

	is_night = night;

	/* Nothing to tell until the phone asks for the readings */
	if (!night_started) {
		return;
	}

	if (send_event(AA_SENSOR_TYPE_NIGHT_DATA) != 0) {
		LOG_WRN("Cannot report the light level");
	}
}

void aa_sensor_link_down(void)
{
	night_started = false;
	is_night = false;
}
