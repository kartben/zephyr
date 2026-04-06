/*
 * Copyright (c) 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/wuc.h>
#include <zephyr/pm/pm.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(wuc_sample, LOG_LEVEL_INF);

/*
 * The sample expects one or more wakeup sources listed in the board's
 * ``/zephyr,user`` node:
 *
 *   / {
 *       zephyr_user: zephyr,user {
 *           wakeup-ctrls = <&wuc0 WAKEUP_PIN_0>;
 *       };
 *   };
 */

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

#if !DT_NODE_HAS_PROP(ZEPHYR_USER_NODE, wakeup_ctrls)
#error "No wakeup sources defined in /zephyr,user. Add a 'wakeup-ctrls' property."
#endif

#define WUC_SPEC(node_id, prop, idx) WUC_DT_SPEC_GET_BY_IDX(node_id, idx),

static const struct wuc_dt_spec wakeup_sources[] = {
	DT_FOREACH_PROP_ELEM(ZEPHYR_USER_NODE, wakeup_ctrls, WUC_SPEC)
};

static const size_t num_wakeup_sources = ARRAY_SIZE(wakeup_sources);

static int enable_all_wakeup_sources(void)
{
	for (size_t i = 0; i < num_wakeup_sources; i++) {
		if (!device_is_ready(wakeup_sources[i].dev)) {
			LOG_ERR("WUC device %s is not ready",
				wakeup_sources[i].dev->name);
			return -ENODEV;
		}

		int ret = wuc_enable_wakeup_source_dt(&wakeup_sources[i]);

		if (ret < 0) {
			LOG_ERR("Failed to enable wakeup source %zu: %d", i, ret);
			return ret;
		}
	}

	LOG_INF("Enabled %zu wakeup source(s)", num_wakeup_sources);
	return 0;
}

static void check_and_clear_triggered_sources(void)
{
	for (size_t i = 0; i < num_wakeup_sources; i++) {
		int ret = wuc_check_wakeup_source_triggered_dt(&wakeup_sources[i]);

		if (ret > 0) {
			LOG_INF("  Wakeup source %zu triggered", i);
			wuc_clear_wakeup_source_triggered_dt(&wakeup_sources[i]);
		} else if (ret == -ENOSYS) {
			LOG_DBG("  Wakeup source %zu: triggered check not supported", i);
		}
	}
}

int main(void)
{
	int ret;
	int cycle = 0;

	LOG_INF("WUC sample started (%zu wakeup source(s))",
		num_wakeup_sources);

	ret = enable_all_wakeup_sources();
	if (ret < 0) {
		return ret;
	}

	while (true) {
		cycle++;
		LOG_INF("Cycle %d: entering low-power state", cycle);

		/*
		 * Request the lowest available power state for the next sleep.
		 * The PM subsystem will select the deepest state permitted by
		 * all enabled wakeup sources and the idle time.
		 */
		k_sleep(K_SECONDS(5));

		LOG_INF("Cycle %d: woke up", cycle);

		check_and_clear_triggered_sources();

		LOG_INF("Cycle %d: re-enabling wakeup sources", cycle);
		ret = enable_all_wakeup_sources();
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}
