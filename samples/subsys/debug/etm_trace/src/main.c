/*
 * Copyright (c) 2026 Benjamin Cabé
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/debug/coresight/etm.h>

/* Large enough to hold the whole on-chip ETB capture for this sample. */
#define TRACE_BUF_SIZE 4096
static uint8_t trace_buf[TRACE_BUF_SIZE];

/*
 * A small, deterministic workload whose instruction stream is captured by the
 * ETM. It is marked noinline so the trace contains recognizable call/return
 * activity when decoded offline.
 */
static uint32_t __noinline workload_fib(uint32_t n)
{
	if (n < 2) {
		return n;
	}

	return workload_fib(n - 1) + workload_fib(n - 2);
}

int main(void)
{
	int err;

	printk("Arm CoreSight ETM instruction trace sample\n");

	err = etm_trace_start();
	if (err) {
		printk("Failed to start ETM trace (%d)\n", err);
		return 0;
	}

	/* Run the workload while tracing is active. */
	volatile uint32_t result = workload_fib(12);

	err = etm_trace_stop();
	if (err) {
		printk("Failed to stop ETM trace (%d)\n", err);
		return 0;
	}

	printk("Workload result: %u\n", result);

	ssize_t len = etm_trace_read(trace_buf, sizeof(trace_buf));

	if (len < 0) {
		printk("Failed to read ETM trace (%d)\n", (int)len);
		return 0;
	}

	if (len == 0) {
		printk("No trace captured. Check the CoreSight fabric routing.\n");
		return 0;
	}

	/*
	 * Dump the raw CoreSight trace bytes as hex. Capture this block from the
	 * console, strip the markers, convert it back to a binary file and decode
	 * it offline (e.g. with OpenCSD or ptm2human) against zephyr.elf.
	 */
	printk("Captured %d bytes of ETM trace:\n", (int)len);
	printk("---8<--- ETM TRACE BEGIN ---8<---\n");
	for (ssize_t i = 0; i < len; i++) {
		printk("%02x", trace_buf[i]);
		if ((i % 32) == 31) {
			printk("\n");
		}
	}
	printk("\n---8<--- ETM TRACE END ---8<---\n");

	return 0;
}
