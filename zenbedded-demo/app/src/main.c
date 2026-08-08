/*
 * Copyright (c) 2026 zenbedded contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zenbedded/control.h>
#include <zenbedded/hist.h>
#include <zenbedded/joint.h>

LOG_MODULE_REGISTER(zenbedded, CONFIG_ZENBEDDED_LOG_LEVEL);

/*
 * main() is the telemetry thread for Phase 1: preemptible, lower priority than
 * the control thread, and free to log. It reads runtime state without any lock,
 * which is safe in exactly one direction: it may observe a torn view of a joint
 * that is being updated underneath it, and for a 1 Hz console report that is
 * fine. Nothing here feeds back into control.
 *
 * Phase 2 replaces this with the comms thread.
 */

static void zb_report(void)
{
	for (uint8_t i = 0; i < ZB_JOINT_COUNT; i++) {
		const struct zb_joint_rt *rt = &zb_rt[i];

		LOG_INF("%s/%s: %-9s pos=%7.3f rad vel=%7.3f rad/s effort=%7.4f Nm %s "
			"(acc=%u rej=%u)",
			ZB_ROBOT_ID, rt->cfg->name, zb_mode_name(rt->mode), (double)rt->position,
			(double)rt->velocity, (double)rt->effort,
			rt->driver_enabled ? "driven" : "coasting", rt->accepted_cmds,
			rt->rejected_cmds);
	}

	const struct zb_hist *h = &zb_loop_hist;

	if (h->count == 0U) {
		return;
	}

	/*
	 * p50, p99, p99.9 and max together. A max on its own tells you nothing
	 * about whether the loop is healthy.
	 */
	LOG_INF("loop: n=%u p50=%uus p99=%uus p99.9=%uus max=%uus min=%uus overruns=%u", h->count,
		zb_hist_percentile(h, 500U), zb_hist_percentile(h, 990U),
		zb_hist_percentile(h, 999U), h->max_us, h->min_us, h->overruns);
}

int main(void)
{
	LOG_INF("zenbedded up on %s: robot '%s', %d joint(s), %d us loop", CONFIG_BOARD_TARGET,
		ZB_ROBOT_ID, ZB_JOINT_COUNT, CONFIG_ZENBEDDED_CONTROL_PERIOD_US);
	LOG_INF("no transport yet (Phase 1): the joint will park and stay parked");

	for (;;) {
		k_sleep(K_SECONDS(1));
		zb_report();
	}

	return 0;
}
