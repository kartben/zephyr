/*
 * Copyright (c) 2026 Carlo Caione <ccaione@baylibre.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/buzzer.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define BUZZER_NODE DT_ALIAS(buzzer)
BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(BUZZER_NODE),
	     "No default buzzer specified in DT (add `aliases { buzzer = ... };`)");

#define RTTTL_TUNE "Scale:d=8,o=5,b=180:c,e,g,2c6"

int main(void)
{
	const struct device *buzzer = DEVICE_DT_GET(BUZZER_NODE);
	int ret;

	if (!device_is_ready(buzzer)) {
		printk("Buzzer device %s not ready\n", buzzer->name);
		return 0;
	}

	ret = buzzer_set_volume(buzzer, 50);
	if (ret < 0) {
		printk("Failed to set buzzer volume (%d)\n", ret);
		return 0;
	}

	while (1) {
		printk("Attention beep\n");
		ret = buzzer_beep(buzzer, 150);
		if (ret < 0) {
			printk("Failed to play beep (%d)\n", ret);
			return 0;
		}
		k_sleep(K_MSEC(400));

		printk("Playing RTTTL tune\n");
		ret = buzzer_play_rttl(buzzer, RTTTL_TUNE);
		if (ret < 0) {
			printk("Failed to play RTTTL tune (%d)\n", ret);
			return 0;
		}

		k_sleep(K_SECONDS(3));
	}
	return 0;
}
