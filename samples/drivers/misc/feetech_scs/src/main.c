/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/drivers/misc/feetech_scs/feetech_scs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

/* About 15 degrees on an SCS0009 */
#define MOVE_OFFSET  50
#define MOVE_TIME_MS 500

#define SERVO_DEVICE(node_id) DEVICE_DT_GET(node_id),

static const struct device *const servos[] = {
	DT_FOREACH_STATUS_OKAY(feetech_scs0009, SERVO_DEVICE)
};

static int print_status(const struct device *servo, uint16_t *position)
{
	struct feetech_scs_status status;
	int ret;

	ret = feetech_scs_get_status(servo, &status);
	if (ret < 0) {
		printf("%s: failed to read status (%d)\n", servo->name, ret);
		return ret;
	}

	printf("%s: position %u, load %d, %u.%u V, %u C\n", servo->name, status.position,
	       status.load, status.voltage / 10U, status.voltage % 10U, status.temperature);

	*position = status.position;

	return 0;
}

static int nudge(const struct device *servo, uint16_t start)
{
	uint16_t target;
	int ret;

	if (start + MOVE_OFFSET <= FEETECH_SCS_POSITION_MAX) {
		target = start + MOVE_OFFSET;
	} else {
		target = start - MOVE_OFFSET;
	}

	ret = feetech_scs_set_torque(servo, true);
	if (ret < 0) {
		return ret;
	}

	printf("%s: moving to %u and back\n", servo->name, target);

	ret = feetech_scs_set_position(servo, target, MOVE_TIME_MS, 0);
	if (ret < 0) {
		return ret;
	}
	k_msleep(MOVE_TIME_MS * 2);

	ret = feetech_scs_set_position(servo, start, MOVE_TIME_MS, 0);
	if (ret < 0) {
		return ret;
	}
	k_msleep(MOVE_TIME_MS * 2);

	return feetech_scs_set_torque(servo, false);
}

int main(void)
{
	uint16_t position[ARRAY_SIZE(servos)];
	int ret;

	for (size_t i = 0; i < ARRAY_SIZE(servos); i++) {
		if (!device_is_ready(servos[i])) {
			printf("%s: device not ready\n", servos[i]->name);
			return 0;
		}

		ret = feetech_scs_ping(servos[i]);
		if (ret < 0) {
			printf("%s: no answer (%d)\n", servos[i]->name, ret);
			return 0;
		}

		ret = print_status(servos[i], &position[i]);
		if (ret < 0) {
			return 0;
		}
	}

	for (size_t i = 0; i < ARRAY_SIZE(servos); i++) {
		ret = nudge(servos[i], position[i]);
		if (ret < 0) {
			printf("%s: move failed (%d)\n", servos[i]->name, ret);
			return 0;
		}
	}

	printf("Torque disabled, move the servos by hand\n");

	while (true) {
		for (size_t i = 0; i < ARRAY_SIZE(servos); i++) {
			ret = feetech_scs_get_position(servos[i], &position[i]);
			if (ret < 0) {
				printf("%s: failed to read position (%d)\n", servos[i]->name, ret);
			} else {
				printf("%s: position %u\n", servos[i]->name, position[i]);
			}
		}

		k_msleep(500);
	}

	return 0;
}
