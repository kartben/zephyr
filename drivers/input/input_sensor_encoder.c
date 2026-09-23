/*
 * Copyright 2026 Benjamin Cabé
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_input_sensor_encoder

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(input_sensor_encoder, CONFIG_INPUT_LOG_LEVEL);

/* Full turn expressed in the millidegree unit used for SENSOR_CHAN_ROTATION. */
#define SENSOR_ENCODER_FULL_TURN 360000

struct sensor_encoder_config {
	const struct device *sensor;
	enum sensor_channel chan;
	uint32_t poll_period_ms;
	int32_t steps_per_period;
	int64_t rollover;
	uint16_t axis;
	bool invert_direction;
};

struct sensor_encoder_data {
	struct k_timer timer;
	struct k_thread thread;

	K_KERNEL_STACK_MEMBER(thread_stack, CONFIG_INPUT_SENSOR_ENCODER_THREAD_STACK_SIZE);

	int64_t prev;
	int64_t acc;
	atomic_t resync;
#ifdef CONFIG_PM_DEVICE
	atomic_t suspended;
	struct k_sem wakeup;
#endif
};

static int sensor_encoder_read(const struct device *dev, int64_t *raw)
{
	const struct sensor_encoder_config *cfg = dev->config;
	struct sensor_value val;
	int ret;

	ret = sensor_sample_fetch_chan(cfg->sensor, cfg->chan);
	if (ret < 0) {
		return ret;
	}

	ret = sensor_channel_get(cfg->sensor, cfg->chan, &val);
	if (ret < 0) {
		return ret;
	}

	if (cfg->chan == SENSOR_CHAN_ROTATION) {
		/* Scale the angle to millidegrees so that a step finer than one
		 * degree does not get quantized away.
		 */
		*raw = sensor_value_to_milli(&val);
	} else {
		*raw = val.val1;
	}

	return 0;
}

/* Fold a raw difference back into [-rollover / 2, rollover / 2) so that a raw
 * value wrapping around is reported as a small movement in the right direction
 * rather than a large one in the wrong one.
 */
static int64_t sensor_encoder_fold(int64_t delta, int64_t rollover)
{
	int64_t half = rollover / 2;

	return (((delta + half) % rollover) + rollover) % rollover - half;
}

static void sensor_encoder_loop(const struct device *dev)
{
	const struct sensor_encoder_config *cfg = dev->config;
	struct sensor_encoder_data *data = dev->data;
	int64_t raw, delta, steps;
	int ret;

	ret = sensor_encoder_read(dev, &raw);
	if (ret < 0) {
		LOG_WRN("Failed to sample %s: %d", cfg->sensor->name, ret);
		return;
	}

	if (atomic_cas(&data->resync, 1, 0)) {
		/* First sample, or first one after a resume: take it as the
		 * new reference without reporting any movement.
		 */
		data->prev = raw;
		return;
	}

	delta = raw - data->prev;
	data->prev = raw;

	if (cfg->rollover != 0) {
		delta = sensor_encoder_fold(delta, cfg->rollover);
	}

	data->acc += delta;

	steps = data->acc / cfg->steps_per_period;
	if (steps == 0) {
		return;
	}

	/* Keep the remainder so that no movement is lost across samples. */
	data->acc -= steps * cfg->steps_per_period;

	if (cfg->invert_direction) {
		steps = -steps;
	}

	input_report_rel(dev, cfg->axis, (int32_t)CLAMP(steps, INT32_MIN, INT32_MAX), true,
			 K_FOREVER);
}

static void sensor_encoder_thread(void *arg1, void *arg2, void *arg3)
{
	const struct device *dev = arg1;
	const struct sensor_encoder_config *cfg = dev->config;
	struct sensor_encoder_data *data = dev->data;

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	if (!device_is_ready(cfg->sensor)) {
		LOG_ERR_DEVICE_NOT_READY(cfg->sensor);
		return;
	}

	while (true) {
#ifdef CONFIG_PM_DEVICE
		if (atomic_get(&data->suspended) == 1) {
			k_sem_take(&data->wakeup, K_FOREVER);
		}
#endif

		sensor_encoder_loop(dev);
		k_timer_status_sync(&data->timer);
	}
}

static int sensor_encoder_init(const struct device *dev)
{
	const struct sensor_encoder_config *cfg = dev->config;
	struct sensor_encoder_data *data = dev->data;
	k_tid_t tid;

	k_timer_init(&data->timer, NULL, NULL);

	atomic_set(&data->resync, 1);

#ifdef CONFIG_PM_DEVICE
	k_sem_init(&data->wakeup, 0, 1);
#endif

	tid = k_thread_create(&data->thread, data->thread_stack,
			      K_KERNEL_STACK_SIZEOF(data->thread_stack),
			      sensor_encoder_thread, (void *)dev, NULL, NULL,
			      CONFIG_INPUT_SENSOR_ENCODER_THREAD_PRIORITY,
			      0, K_NO_WAIT);
	if (!tid) {
		LOG_ERR("thread creation failed");
		return -ENODEV;
	}

	k_thread_name_set(&data->thread, dev->name);

	k_timer_start(&data->timer, K_MSEC(cfg->poll_period_ms), K_MSEC(cfg->poll_period_ms));

	return 0;
}

#ifdef CONFIG_PM_DEVICE
static int sensor_encoder_pm_action(const struct device *dev, enum pm_device_action action)
{
	const struct sensor_encoder_config *cfg = dev->config;
	struct sensor_encoder_data *data = dev->data;

	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND:
		atomic_set(&data->suspended, 1);
		k_timer_stop(&data->timer);
		break;
	case PM_DEVICE_ACTION_RESUME:
		/* The encoder may have moved while suspended, resynchronize on
		 * the next sample instead of reporting a bogus jump.
		 */
		atomic_set(&data->resync, 1);
		data->acc = 0;

		k_timer_start(&data->timer, K_MSEC(cfg->poll_period_ms),
			      K_MSEC(cfg->poll_period_ms));
		atomic_set(&data->suspended, 0);
		k_sem_give(&data->wakeup);
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}
#endif

#define SENSOR_ENCODER_CHAN(inst) \
	UTIL_CAT(SENSOR_CHAN_, DT_INST_STRING_UPPER_TOKEN(inst, channel))

/* A rotation angle wraps around once per turn, a raw count only wraps around at
 * a device specific value that has to be set explicitly.
 */
#define SENSOR_ENCODER_ROLLOVER(inst)							\
	DT_INST_PROP_OR(inst, rollover,							\
			COND_CODE_1(DT_INST_ENUM_HAS_VALUE(inst, channel, rotation),	\
				    (SENSOR_ENCODER_FULL_TURN), (0)))

#define SENSOR_ENCODER_INIT(inst)							\
	BUILD_ASSERT(DT_INST_PROP(inst, steps_per_period) > 0,				\
		     "steps-per-period must be greater than zero");			\
											\
	static const struct sensor_encoder_config sensor_encoder_cfg_##inst = {		\
		.sensor = DEVICE_DT_GET(DT_INST_PHANDLE(inst, sensor)),			\
		.chan = SENSOR_ENCODER_CHAN(inst),					\
		.poll_period_ms = DT_INST_PROP(inst, poll_period_ms),			\
		.steps_per_period = DT_INST_PROP(inst, steps_per_period),		\
		.rollover = SENSOR_ENCODER_ROLLOVER(inst),				\
		.axis = DT_INST_PROP(inst, zephyr_axis),				\
		.invert_direction = DT_INST_PROP(inst, invert_direction),		\
	};										\
											\
	static struct sensor_encoder_data sensor_encoder_data_##inst;			\
											\
	PM_DEVICE_DT_INST_DEFINE(inst, sensor_encoder_pm_action);			\
											\
	DEVICE_DT_INST_DEFINE(inst, sensor_encoder_init,				\
			      PM_DEVICE_DT_INST_GET(inst),				\
			      &sensor_encoder_data_##inst,				\
			      &sensor_encoder_cfg_##inst,				\
			      POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY,			\
			      NULL);

DT_INST_FOREACH_STATUS_OKAY(SENSOR_ENCODER_INIT)
