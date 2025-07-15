/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 Jilay Sandeep Pandya.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/stepper.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/paj7620.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(stepper_gesture, CONFIG_STEPPER_LOG_LEVEL);

static const struct device *stepper = DEVICE_DT_GET(DT_ALIAS(stepper));
static const struct device *gesture_sensor = DEVICE_DT_GET_ONE(pixart_paj7620);

#define GESTURE_POLL_TIME_MS   50
#define MIN_STEP_INTERVAL_NS   100000   /* 100 microseconds minimum */
#define MAX_STEP_INTERVAL_NS   10000000 /* 10 milliseconds maximum */
#define STEP_INTERVAL_DELTA_NS 500000   /* 500 microseconds increment */

enum stepper_state {
	STEPPER_STATE_STOPPED,
	STEPPER_STATE_RUNNING_CW,
	STEPPER_STATE_RUNNING_CCW,
};

static atomic_t stepper_state = ATOMIC_INIT(STEPPER_STATE_STOPPED);
static atomic_t step_interval_ns = ATOMIC_INIT(CONFIG_STEP_INTERVAL_NS);
static atomic_t stepper_enabled = ATOMIC_INIT(0);

static void stepper_callback(const struct device *dev, const enum stepper_event event,
			     void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	switch (event) {
	case STEPPER_EVENT_STEPS_COMPLETED:
		LOG_DBG("Steps completed");
		break;
	case STEPPER_EVENT_STOPPED:
		LOG_DBG("Stepper stopped");
		break;
	case STEPPER_EVENT_FAULT_DETECTED:
		LOG_WRN("Stepper fault detected");
		break;
	default:
		LOG_WRN("Unknown stepper event: %d", event);
		break;
	}
}

static void handle_gesture_up(void)
{
	/* Accelerate - decrease step interval */
	int32_t current_interval = atomic_get(&step_interval_ns);
	int32_t new_interval = current_interval - STEP_INTERVAL_DELTA_NS;

	if (new_interval < MIN_STEP_INTERVAL_NS) {
		new_interval = MIN_STEP_INTERVAL_NS;
	}

	atomic_set(&step_interval_ns, new_interval);
	int ret = stepper_set_microstep_interval(stepper, new_interval);
	if (ret != 0) {
		LOG_ERR("Failed to set step interval: %d", ret);
		return;
	}
	LOG_INF("Accelerate: step interval now %d ns", new_interval);
}

static void handle_gesture_down(void)
{
	/* Decelerate - increase step interval */
	int32_t current_interval = atomic_get(&step_interval_ns);
	int32_t new_interval = current_interval + STEP_INTERVAL_DELTA_NS;

	if (new_interval > MAX_STEP_INTERVAL_NS) {
		new_interval = MAX_STEP_INTERVAL_NS;
	}

	atomic_set(&step_interval_ns, new_interval);
	int ret = stepper_set_microstep_interval(stepper, new_interval);
	if (ret != 0) {
		LOG_ERR("Failed to set step interval: %d", ret);
		return;
	}
	LOG_INF("Decelerate: step interval now %d ns", new_interval);
}

static void handle_gesture_clockwise(void)
{
	/* Change direction to clockwise */
	atomic_set(&stepper_state, STEPPER_STATE_RUNNING_CW);
	if (atomic_get(&stepper_enabled) == 1) {
		int ret = stepper_run(stepper, STEPPER_DIRECTION_POSITIVE);
		if (ret != 0) {
			LOG_ERR("Failed to run stepper clockwise: %d", ret);
			return;
		}
		LOG_INF("Direction: Clockwise");
	}
}

static void handle_gesture_counterclockwise(void)
{
	/* Change direction to counter-clockwise */
	atomic_set(&stepper_state, STEPPER_STATE_RUNNING_CCW);
	if (atomic_get(&stepper_enabled) == 1) {
		int ret = stepper_run(stepper, STEPPER_DIRECTION_NEGATIVE);
		if (ret != 0) {
			LOG_ERR("Failed to run stepper counter-clockwise: %d", ret);
			return;
		}
		LOG_INF("Direction: Counter-clockwise");
	}
}

static void handle_gesture_forward(void)
{
	/* Start stepper */
	if (atomic_get(&stepper_enabled) == 0) {
		int ret = stepper_enable(stepper);
		if (ret != 0) {
			LOG_ERR("Failed to enable stepper: %d", ret);
			return;
		}
		atomic_set(&stepper_enabled, 1);
		LOG_INF("Stepper enabled");
	}

	enum stepper_state state = atomic_get(&stepper_state);
	int ret;
	switch (state) {
	case STEPPER_STATE_RUNNING_CW:
		ret = stepper_run(stepper, STEPPER_DIRECTION_POSITIVE);
		if (ret != 0) {
			LOG_ERR("Failed to start stepper clockwise: %d", ret);
			return;
		}
		LOG_INF("Started: Clockwise");
		break;
	case STEPPER_STATE_RUNNING_CCW:
		ret = stepper_run(stepper, STEPPER_DIRECTION_NEGATIVE);
		if (ret != 0) {
			LOG_ERR("Failed to start stepper counter-clockwise: %d", ret);
			return;
		}
		LOG_INF("Started: Counter-clockwise");
		break;
	default:
		/* Default to clockwise if no direction set */
		atomic_set(&stepper_state, STEPPER_STATE_RUNNING_CW);
		ret = stepper_run(stepper, STEPPER_DIRECTION_POSITIVE);
		if (ret != 0) {
			LOG_ERR("Failed to start stepper (default clockwise): %d", ret);
			return;
		}
		LOG_INF("Started: Clockwise (default)");
		break;
	}
}

static void handle_gesture_backward(void)
{
	/* Stop stepper */
	int ret = stepper_stop(stepper);
	if (ret != 0) {
		LOG_ERR("Failed to stop stepper: %d", ret);
		return;
	}
	atomic_set(&stepper_state, STEPPER_STATE_STOPPED);
	LOG_INF("Stepper stopped");
}

static void process_gestures(uint16_t gest_flags)
{
	if ((gest_flags & PAJ7620_FLAG_GES_UP) != 0) {
		handle_gesture_up();
	}
	if ((gest_flags & PAJ7620_FLAG_GES_DOWN) != 0) {
		handle_gesture_down();
	}
	if ((gest_flags & PAJ7620_FLAG_GES_CLOCKWISE) != 0) {
		handle_gesture_clockwise();
	}
	if ((gest_flags & PAJ7620_FLAG_GES_COUNTERCLOCKWISE) != 0) {
		handle_gesture_counterclockwise();
	}
	if ((gest_flags & PAJ7620_FLAG_GES_FORWARD) != 0) {
		handle_gesture_forward();
	}
	if ((gest_flags & PAJ7620_FLAG_GES_BACKWARD) != 0) {
		handle_gesture_backward();
	}
}

static void gesture_polling_loop(void)
{
	struct sensor_value data;
	int retry_count = 0;
	const int max_retries = 5;

	while (1) {
		int ret = sensor_sample_fetch(gesture_sensor);
		if (ret < 0) {
			LOG_ERR("sensor_sample_fetch failed: %d", ret);
			retry_count++;
			if (retry_count >= max_retries) {
				LOG_ERR("Too many sensor failures, continuing anyway");
				retry_count = 0;
			}
			k_msleep(GESTURE_POLL_TIME_MS);
			continue;
		}

		ret = sensor_channel_get(gesture_sensor,
					 (enum sensor_channel)SENSOR_CHAN_PAJ7620_GESTURES, &data);
		if (ret < 0) {
			LOG_ERR("sensor_channel_get failed: %d", ret);
			retry_count++;
			if (retry_count >= max_retries) {
				LOG_ERR("Too many sensor failures, continuing anyway");
				retry_count = 0;
			}
			k_msleep(GESTURE_POLL_TIME_MS);
			continue;
		}

		retry_count = 0; /* Reset retry count on successful read */

		if (data.val1 != 0) {
			process_gestures(data.val1);
		}

		k_msleep(GESTURE_POLL_TIME_MS);
	}
}

int main(void)
{
	LOG_INF("Starting stepper with gesture control sample");

	/* Check stepper device */
	if (!device_is_ready(stepper)) {
		LOG_ERR("Stepper device %s is not ready", stepper->name);
		return -ENODEV;
	}
	LOG_INF("Stepper device: %s", stepper->name);

	/* Check gesture sensor device */
	if (!device_is_ready(gesture_sensor)) {
		LOG_ERR("Gesture sensor device %s is not ready", gesture_sensor->name);
		return -ENODEV;
	}
	LOG_INF("Gesture sensor device: %s", gesture_sensor->name);

	/* Initialize stepper with error checking */
	int ret = stepper_set_event_callback(stepper, stepper_callback, NULL);
	if (ret != 0) {
		LOG_ERR("Failed to set stepper callback: %d", ret);
		return ret;
	}

	ret = stepper_set_reference_position(stepper, 0);
	if (ret != 0) {
		LOG_ERR("Failed to set reference position: %d", ret);
		return ret;
	}

	ret = stepper_set_microstep_interval(stepper, CONFIG_STEP_INTERVAL_NS);
	if (ret != 0) {
		LOG_ERR("Failed to set microstep interval: %d", ret);
		return ret;
	}

	LOG_INF("Gesture controls:");
	LOG_INF("  UP    - Accelerate");
	LOG_INF("  DOWN  - Decelerate");
	LOG_INF("  CW    - Clockwise direction");
	LOG_INF("  CCW   - Counter-clockwise direction");
	LOG_INF("  FORWARD  - Start stepper");
	LOG_INF("  BACKWARD - Stop stepper");

	/* Start gesture polling loop */
	gesture_polling_loop();

	return 0;
}

static void monitor_thread(void)
{
	for (;;) {
		int32_t actual_position;

		int ret = stepper_get_actual_position(stepper, &actual_position);
		if (ret == 0) {
			LOG_DBG("Actual position: %d", actual_position);
		} else {
			LOG_WRN("Failed to get stepper position: %d", ret);
		}
		k_sleep(K_MSEC(CONFIG_MONITOR_THREAD_TIMEOUT_MS));
	}
}

K_THREAD_DEFINE(monitor_tid, CONFIG_MONITOR_THREAD_STACK_SIZE, monitor_thread, NULL, NULL, NULL, 5,
		0, 0);
