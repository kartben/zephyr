/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "edge264_zephyr.h"

#if defined(CONFIG_EDGE264_HEAP_ZEPHYR_REGION)
#define EDGE264_HEAP_REGION Z_GENERIC_SECTION(CONFIG_EDGE264_HEAP_ZEPHYR_REGION_NAME)
#else
#define EDGE264_HEAP_REGION __noinit_named(kheap_buf_edge264)
#endif

static char EDGE264_HEAP_REGION __aligned(64)
	edge264_heap_mem[MAX(CONFIG_EDGE264_HEAP_SIZE, Z_HEAP_MIN_SIZE)];
static struct k_heap edge264_heap;
static bool edge264_heap_ready;

void *edge264_z_aligned_alloc(size_t align, size_t size)
{
	if (!edge264_heap_ready) {
		k_heap_init(&edge264_heap, edge264_heap_mem, sizeof(edge264_heap_mem));
		edge264_heap_ready = true;
	}

	if (align < sizeof(void *)) {
		align = sizeof(void *);
	}

	return k_heap_aligned_alloc(&edge264_heap, align, size, K_NO_WAIT);
}

void edge264_z_free(void *ptr)
{
	if (ptr == NULL) {
		return;
	}

	if (!edge264_heap_ready) {
		return;
	}

	k_heap_free(&edge264_heap, ptr);
}

int edge264_z_clock_gettime(int clk, struct timespec *tp)
{
	uint64_t us = k_ticks_to_us_near64(k_uptime_ticks());

	ARG_UNUSED(clk);
	tp->tv_sec = (time_t)(us / 1000000U);
	tp->tv_nsec = (long)(us % 1000000U) * 1000L;
	return 0;
}
