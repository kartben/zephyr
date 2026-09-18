/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "aa_demo.h"
#include "aa_session.h"

LOG_MODULE_REGISTER(aa_hu_main, CONFIG_SAMPLE_AA_HU_LOG_LEVEL);

int main(void)
{
	int ret;

	if (IS_ENABLED(CONFIG_SAMPLE_AA_HU_DEMO_CLIP)) {
		ret = aa_hu_demo_start();
	} else {
		ret = aa_hu_session_start();
	}

	if (ret != 0) {
		LOG_ERR("Head unit start failed (%d)", ret);
	}

	return 0;
}
