/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_tof.h"

#ifdef CONFIG_SAMPLE_AA_HU_TOF_NIGHT

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/rtio/rtio.h>

#include "aa_sensor.h"

LOG_MODULE_REGISTER(aa_tof, CONFIG_SAMPLE_AA_HU_LOG_LEVEL);

/*
 * The board carries no light sensor, so the time-of-flight ranger stands in for
 * one: a hand held over it reads as darkness and the phone redraws everything
 * it projects in its night theme, which is the whole point of the sensor
 * channel and is visible without any interaction. The nearest zone decides,
 * since a hand covers only part of the field of view.
 */

#define TOF_ZONES 16

static const struct device *const tof = DEVICE_DT_GET(DT_ALIAS(aa_tof));

static struct sensor_chan_spec tof_chan = {
	.chan_type = SENSOR_CHAN_DISTANCE,
	.chan_idx = 0,
};

static struct sensor_read_config tof_read_config = {
	.sensor = DEVICE_DT_GET(DT_ALIAS(aa_tof)),
	.is_streaming = false,
	.channels = &tof_chan,
	.count = 1,
	.max = 1,
};

RTIO_IODEV_DEFINE(tof_iodev, &__sensor_iodev_api, &tof_read_config);
RTIO_DEFINE_WITH_MEMPOOL(tof_rtio, 2, 2, 32, 64, 4);

static K_THREAD_STACK_DEFINE(tof_stack, CONFIG_SAMPLE_AA_HU_TOF_STACK_SIZE);
static struct k_thread tof_thread_data;

/* Nearest zone of the last reading in millimetres, or -1 when nothing ranged */
static int32_t nearest_mm;

static void on_reading(int result, uint8_t *buf, uint32_t buf_len, void *userdata)
{
	const struct sensor_decoder_api *decoder;
	struct sensor_chan_spec chan = tof_chan;
	int32_t nearest = INT32_MAX;

	ARG_UNUSED(buf_len);
	ARG_UNUSED(userdata);

	if (result != 0 || sensor_get_decoder(tof, &decoder) != 0) {
		return;
	}

	for (uint32_t zone = 0; zone < TOF_ZONES; zone++) {
		struct sensor_q31_data data = {0};
		uint32_t fit = 0;
		int32_t mm;

		chan.chan_idx = zone;
		if (decoder->decode(buf, chan, &fit, 1, &data) < 1) {
			continue;
		}

		/* The decoder reports metres as q29, and only a positive range is real */
		mm = (int32_t)(((int64_t)data.readings[0].value * 1000) >> 29);
		if (mm > 0 && mm < nearest) {
			nearest = mm;
		}
	}

	nearest_mm = (nearest == INT32_MAX) ? -1 : nearest;
}

static void tof_thread(void *p1, void *p2, void *p3)
{
	bool night = false;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		k_sleep(K_MSEC(CONFIG_SAMPLE_AA_HU_TOF_PERIOD_MS));

		nearest_mm = -1;
		if (sensor_read_async_mempool(&tof_iodev, &tof_rtio, NULL) != 0) {
			LOG_WRN_ONCE("Ranger read failed");
			continue;
		}
		sensor_processing_with_callback(&tof_rtio, on_reading);

		if (nearest_mm < 0) {
			continue;
		}

		/*
		 * Two thresholds rather than one, so that a hand hovering at the
		 * boundary does not make the phone flash between its themes.
		 */
		if (!night && nearest_mm <= CONFIG_SAMPLE_AA_HU_TOF_NEAR_MM) {
			night = true;
		} else if (night && nearest_mm >= CONFIG_SAMPLE_AA_HU_TOF_FAR_MM) {
			night = false;
		} else {
			continue;
		}

		LOG_INF("Nearest object %d mm, reporting %s", nearest_mm,
			night ? "night" : "daylight");
		aa_sensor_set_night(night);
	}
}

int aa_tof_init(void)
{
	struct sensor_value val = {0};

	/* Without one the head unit still projects, it just never turns dark */
	if (!device_is_ready(tof)) {
		LOG_WRN("No ranger, the light level stays as it is");
		return 0;
	}

	val.val1 = TOF_ZONES;
	if (sensor_attr_set(tof, SENSOR_CHAN_DISTANCE, SENSOR_ATTR_RESOLUTION, &val) != 0) {
		LOG_ERR("Cannot set the ranger resolution");
		return -EIO;
	}

	val.val1 = CONFIG_SAMPLE_AA_HU_TOF_RATE_HZ;
	if (sensor_attr_set(tof, SENSOR_CHAN_DISTANCE, SENSOR_ATTR_SAMPLING_FREQUENCY, &val) != 0) {
		LOG_ERR("Cannot set the ranger sampling frequency");
		return -EIO;
	}

	k_thread_create(&tof_thread_data, tof_stack, K_THREAD_STACK_SIZEOF(tof_stack), tof_thread,
			NULL, NULL, NULL, CONFIG_SAMPLE_AA_HU_TOF_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&tof_thread_data, "aa_tof");

	return 0;
}

#else /* CONFIG_SAMPLE_AA_HU_TOF_NIGHT */

int aa_tof_init(void)
{
	return 0;
}

#endif /* CONFIG_SAMPLE_AA_HU_TOF_NIGHT */
