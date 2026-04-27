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

#define SAMPLE_PERIOD_MS 2000
#define RETRY_DELAY_MS   500

static const char *presence_to_string(int status)
{
	switch (status) {
	case MR60BHA2_PRESENCE_NO_TARGET:
		return "No target";
	case MR60BHA2_PRESENCE_STATIC:
		return "Static target";
	case MR60BHA2_PRESENCE_MOVING:
		return "Moving target";
	default:
		return "Unknown";
	}
}

static const char *breathing_phase_to_string(int phase)
{
	switch (phase) {
	case MR60BHA2_BREATHING_DETECTING:
		return "Detecting";
	case MR60BHA2_BREATHING_NORMAL:
		return "Normal breathing";
	case MR60BHA2_BREATHING_RAPID:
		return "Rapid breathing";
	case MR60BHA2_BREATHING_SLOW:
		return "Slow breathing";
	case MR60BHA2_BREATHING_NONE:
		return "No breathing";
	default:
		return "Unknown";
	}
}

static const char *heartbeat_phase_to_string(int phase)
{
	switch (phase) {
	case MR60BHA2_HEARTBEAT_DETECTING:
		return "Detecting";
	case MR60BHA2_HEARTBEAT_NORMAL:
		return "Normal heartbeat";
	case MR60BHA2_HEARTBEAT_RAPID:
		return "Rapid heartbeat";
	case MR60BHA2_HEARTBEAT_SLOW:
		return "Slow heartbeat";
	case MR60BHA2_HEARTBEAT_NONE:
		return "No heartbeat";
	default:
		return "Unknown";
	}
}

int main(void)
{
	const struct device *radar = DEVICE_DT_GET(RADAR_NODE);
	struct sensor_value val;
	int rc;

	if (!device_is_ready(radar)) {
		printk("Radar device %s is not ready\n", radar->name);
		return 0;
	}

	printk("Seeed Studio MR60BHA2 sample started on %s\n", radar->name);

	while (1) {
		rc = sensor_sample_fetch(radar);
		if (rc != 0) {
			printk("sample_fetch failed: %d\n", rc);
			k_sleep(K_MSEC(RETRY_DELAY_MS));
			continue;
		}

		printk("MR60BHA2 60 GHz mmWave Sensor\n");
		printk("==============================\n\n");

		/* Presence */
		rc = sensor_channel_get(radar, SENSOR_CHAN_PROX, &val);
		printk("  %-18s %s\n", "Presence:", val.val1 ? "occupied" : "clear");

		rc = sensor_channel_get(radar,
					(enum sensor_channel)SENSOR_CHAN_MR60BHA2_PRESENCE_STATUS,
					&val);
		if (rc == 0) {
			printk("  %-18s %s\n", "Presence status:", presence_to_string(val.val1));
		}

		/* Breathing */
		printk("\n  Breathing\n");
		printk("  *********\n\n");

		rc = sensor_channel_get(radar,
					(enum sensor_channel)SENSOR_CHAN_MR60BHA2_BREATHING_PHASE,
					&val);
		printk("  %-18s %s\n", "Phase:",
		       (rc == 0) ? breathing_phase_to_string(val.val1) : "n/a");

		rc = sensor_channel_get(radar,
					(enum sensor_channel)SENSOR_CHAN_MR60BHA2_BREATHING_RATE,
					&val);
		if (rc == 0) {
			printk("  %-18s %.1f breaths/min\n", "Rate:",
			       sensor_value_to_double(&val));
		} else {
			printk("  %-18s n/a\n", "Rate:");
		}

		/* Heartbeat */
		printk("\n  Heartbeat\n");
		printk("  *********\n\n");

		rc = sensor_channel_get(radar,
					(enum sensor_channel)SENSOR_CHAN_MR60BHA2_HEARTBEAT_PHASE,
					&val);
		printk("  %-18s %s\n", "Phase:",
		       (rc == 0) ? heartbeat_phase_to_string(val.val1) : "n/a");

		rc = sensor_channel_get(radar,
					(enum sensor_channel)SENSOR_CHAN_MR60BHA2_HEARTBEAT_RATE,
					&val);
		if (rc == 0) {
			printk("  %-18s %d bpm\n", "Rate:", val.val1);
		} else {
			printk("  %-18s n/a\n", "Rate:");
		}

		printk("\n");
		k_sleep(K_MSEC(SAMPLE_PERIOD_MS));
	}
}
