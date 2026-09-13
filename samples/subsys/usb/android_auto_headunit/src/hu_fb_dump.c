/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hu_fb_dump.h"

#include <errno.h>

#include <zephyr/logging/log.h>

#include "hu_fb_dump_bottom.h"

LOG_MODULE_REGISTER(hu_fb_dump, CONFIG_SAMPLE_AA_HU_LOG_LEVEL);

static int fd = -1;

int hu_fb_dump_open(void)
{
	fd = hu_fb_dump_bottom_open(CONFIG_SAMPLE_AA_HU_FB_DUMP_PATH);
	if (fd < 0) {
		LOG_ERR("Cannot create %s", CONFIG_SAMPLE_AA_HU_FB_DUMP_PATH);
		return -EIO;
	}

	LOG_INF("Dumping decoded pictures to %s (RGB565)", CONFIG_SAMPLE_AA_HU_FB_DUMP_PATH);

	return 0;
}

int hu_fb_dump_write(const uint16_t *fb, size_t pixels)
{
	if (fd < 0) {
		return 0;
	}

	if (hu_fb_dump_bottom_write(fd, fb, pixels * sizeof(uint16_t)) < 0) {
		return -EIO;
	}

	return 0;
}
