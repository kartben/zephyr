/*
 * Copyright (c) 2024 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_device, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>
#include <zephyr/drivers/ptp_clock.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/led.h>
#include <zephyr/random/random.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/ethernet.h>

#include "device.h"

#include <time.h>

#include <zephyr/input/input.h>


#define SENSOR_CHAN     SENSOR_CHAN_DIE_TEMP
#define SENSOR_UNIT     "Celsius"

/* Devices */
static const struct device *sensor = DEVICE_DT_GET_OR_NULL(DT_ALIAS(asssmbient_temp0));
static const struct device *leds = DEVICE_DT_GET_OR_NULL(DT_INST(0, gpio_leds));

/* Command handlers */
static void led_on_handler(void)
{
	device_write_led(LED_USER, LED_ON);
}

static void led_off_handler(void)
{
	device_write_led(LED_USER, LED_OFF);
}

/* Supported device commands */
struct device_cmd device_commands[] = {
	{"led_on", led_on_handler},
	{"led_off", led_off_handler}
};

const size_t num_device_commands = ARRAY_SIZE(device_commands);

/* Command dispatcher */
void device_command_handler(uint8_t *command)
{
	for (int i = 0; i < num_device_commands; i++) {
		if (strcmp(command, device_commands[i].command) == 0) {
			LOG_INF("Executing device command: %s", device_commands[i].command);
			return device_commands[i].handler();
		}
	}
	LOG_ERR("Unknown command: %s", command);
}

int device_read_sensor(struct sensor_sample *sample)
{
	int rc;
	struct sensor_value sensor_val;

	/* Read sample only if a real sensor device is present
	 * otherwise return a dummy value
	 */
	if (sensor == NULL) {
		sample->unit = SENSOR_UNIT;



#if defined(CONFIG_PTP_CLOCK)
	struct net_if *iface;

	iface = net_if_get_default();
	if (iface == NULL) {
		LOG_ERR("No network interface configured");
		return -ENETDOWN;
	}

	const struct device *clk;
	struct net_ptp_time tm;

	clk = net_eth_get_ptp_clock(iface);
	if(clk) {
		rc = ptp_clock_get(clk, &tm);
		if (rc) {
			LOG_ERR("Failed to get PTP clock time [%d]", rc);
			return rc;
		}
		printf("PTP clock time: %lld.%llu\n", tm.second, tm.nanosecond);

		time_t sec = (time_t)tm.second - 37; /** UTC to TAI offset */
		struct tm *tm_info = gmtime(&sec);

		char buffer[50];
		// Format time as ISO 8601 string with nanoseconds
		strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", tm_info);
		snprintf(buffer, sizeof(buffer), "%s.%dZ", buffer, tm.nanosecond);
	//	strncpy(sample->timestamp, buffer, sizeof(buffer));
		sample->ts.seconds = tm.second - 37;
		sample->ts.nanoseconds = tm.nanosecond;
	} else {
	//	sample->ts = { 0, 0 };
	}

#else
	//sample->ts = { 0, 0 };
#endif




		sample->value = 20.0 + (double)sys_rand32_get() / UINT32_MAX * 5.0;
		return 0;
	}

	rc = sensor_sample_fetch(sensor);
	if (rc) {
		LOG_ERR("Failed to fetch sensor sample [%d]", rc);
		return rc;
	}

	rc = sensor_channel_get(sensor, SENSOR_CHAN, &sensor_val);
	if (rc) {
		LOG_ERR("Failed to get sensor channel [%d]", rc);
		return rc;
	}

	sample->unit = SENSOR_UNIT;

// 	struct net_ptp_time current;

// #if defined(CONFIG_PTP_CLOCK)
// 	struct net_if *iface;

// 	iface = net_if_get_default();
// 	if (iface == NULL) {
// 		LOG_ERR("No network interface configured");
// 		return -ENETDOWN;
// 	}

// 	const struct device *clk;
// 	struct net_ptp_time tm;

// 	clk = net_eth_get_ptp_clock(iface);
// 	if(clk) {
// 		rc = ptp_clock_get(clk, &tm);
// 		if (rc) {
// 			LOG_ERR("Failed to get PTP clock time [%d]", rc);
// 			return rc;
// 		}
// 		printf("PTP clock time: %d.%09d\n", tm.second, tm.nanosecond);
// 		sample->timestamp = "2024-xx";
// 	} else {
// 		sample->timestamp = "N/A";
// 	}

// #else
// 	sample->timestamp = "N/A";
// #endif

	sample->value = sensor_value_to_double(&sensor_val);
	return rc;
}

int device_write_led(enum led_id led_idx, enum led_state state)
{
	int rc;

	switch (state) {
	case LED_OFF:
		if (leds == NULL) {
			LOG_INF("LED %d OFF", led_idx);
			break;
		}
		led_off(leds, led_idx);
		break;
	case LED_ON:
		if (leds == NULL) {
			LOG_INF("LED %d ON", led_idx);
			break;
		}
		led_on(leds, led_idx);
		break;
	default:
		LOG_ERR("Invalid LED state setting");
		rc = -EINVAL;
		break;
	}

	return rc;
}

static void input_dump_cb(struct input_event *evt)
{
	uint64_t event_time = k_cycle_get_64();


	//printk("Input event sysclock in ns = %llu\n", k_cyc_to_ns_floor64(event_time));

	uint64_t hasptpcktime_time;

#if defined(CONFIG_PTP_CLOCK)
	struct net_if *iface;
	int rc;

	iface = net_if_get_default();
	if (iface == NULL) {
		LOG_ERR("No network interface configured");
		return -ENETDOWN;
	}

	const struct device *clk;
	struct net_ptp_time tm;

	clk = net_eth_get_ptp_clock(iface);
	if(clk) {
		rc = ptp_clock_get(clk, &tm);
		if (rc) {
			LOG_ERR("Failed to get PTP clock time [%d]", rc);
			return;
		}

		hasptpcktime_time = k_cycle_get_64();

		// LOG_DBG("cycles between event time and ptp clock acquisition: %u",
		// 	hasptpcktime_time - event_time);
		// LOG_DBG("nanoseconds between event time and ptp clock acquisition: %u",
		// 	k_cyc_to_ns_floor32(hasptpcktime_time - event_time));


		printk("PUSH >>> PTP clock time: %09llu\n", (tm.second * 1000000000L +
			tm.nanosecond -
			k_cyc_to_ns_floor64(hasptpcktime_time - event_time) -
			30 * 1000 * 1000) % 1000000000L);


		time_t sec = (time_t)tm.second - 37; /** UTC to TAI offset */
		struct tm *tm_info = gmtime(&sec);

		// todo!! handle second rollover if nanosecond would end up being negative

		tm.nanosecond -= k_cyc_to_ns_floor64(hasptpcktime_time - event_time);

		// also subtract debounce time (30ms). TODO -- shouldn't be hardcoded!
		tm.nanosecond -= 30 * 1000 * 1000;

		char buffer[50];
		// Format time as ISO 8601 string with nanoseconds
		strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", tm_info);
		snprintf(buffer, sizeof(buffer), "%s.%dZ", buffer, tm.nanosecond);


		// LOG_INF("Received input event at %s", buffer);

	} else {
	//	sample->ts = { 0, 0 };
	}

#else
	//sample->ts = { 0, 0 };
#endif


}
INPUT_CALLBACK_DEFINE(NULL, input_dump_cb);


bool devices_ready(void)
{
	bool rc = true;

	/* Check readiness only if a real sensor device is present */
	if (sensor != NULL) {
		if (!device_is_ready(sensor)) {
			LOG_ERR("Device %s is not ready", sensor->name);
			rc = false;
		} else {
			LOG_INF("Device %s is ready", sensor->name);
		}
	}

	if (leds != NULL) {
		if (!device_is_ready(leds)) {
			LOG_ERR("Device %s is not ready", leds->name);
			rc = false;
		} else {
			LOG_INF("Device %s is ready", leds->name);
		}
	}

	return rc;
}
