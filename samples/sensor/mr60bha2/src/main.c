/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/mr60bha2.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#if DT_HAS_COMPAT_STATUS_OKAY(seeed_mr60bha2)
#define RADAR_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(seeed_mr60bha2)
#else
#error "No enabled seeed,mr60bha2 devicetree node found"
#endif

#define SAMPLE_PERIOD_MS 1000
#define RETRY_DELAY_MS   250

static void print_channel_value(const struct device *dev, const char *label,
			       enum sensor_channel chan)
{
	struct sensor_value val;
	int rc = sensor_channel_get(dev, chan, &val);

	printk("  %-19s ", label);

	if (rc == -ENODATA) {
		printk("n/a\n");
		return;
	} else if (rc != 0) {
		printk("error (%d)\n", rc);
		return;
	}

	switch ((uint32_t)chan) {
	case SENSOR_CHAN_PROX:
		printk("%s\n", val.val1 ? "occupied" : "clear");
		break;
	case SENSOR_CHAN_MR60BHA2_TARGET_COUNT:
		printk("%d\n", val.val1);
		break;
	default:
		printk("%.3f\n", sensor_value_to_double(&val));
		break;
	}
}

int main(void)
{
	const struct device *radar = DEVICE_DT_GET(RADAR_NODE);
	int rc;

	if (!device_is_ready(radar)) {
		printk("Radar device %s is not ready\n", radar->name);
		return 0;
	}

	printk("Seeed MR60BHA2 sample started on %s\n", radar->name);

	while (1) {
		rc = sensor_sample_fetch(radar);
		if (rc != 0) {
			printk("sample_fetch failed: %d\n", rc);
			k_sleep(K_MSEC(RETRY_DELAY_MS));
			continue;
		}

		printk("MR60BHA2 Sensor Report\n");
		printk("======================\n\n");
		print_channel_value(radar, "Presence", SENSOR_CHAN_PROX);
		print_channel_value(radar, "Distance (m)", SENSOR_CHAN_DISTANCE);
		print_channel_value(radar, "Breath rate", SENSOR_CHAN_MR60BHA2_BREATH_RATE);
		print_channel_value(radar, "Heart rate", SENSOR_CHAN_MR60BHA2_HEART_RATE);
		print_channel_value(radar, "Targets", SENSOR_CHAN_MR60BHA2_TARGET_COUNT);
		printk("\n");

		k_sleep(K_MSEC(SAMPLE_PERIOD_MS));
	}
}
