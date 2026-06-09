/*
 * Copyright (c) 2026 Benjamin Cabé
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Arm CoreSight ETM (Embedded Trace Macrocell) instruction trace.
 *
 * This module programs an ETMv4 trace source (for example the ETM-M33 found on
 * Cortex-M33) and an on-chip trace sink (ETB or ETR) so the executed
 * instruction stream can be captured on-target and decoded offline.
 *
 * The CoreSight trace fabric that carries the ETM trace to the sink (ATB
 * funnels, replicators and the trace power domain) is SoC specific and is
 * configured by the platform CoreSight driver during system initialization.
 * This module is intentionally limited to the architecturally defined ETMv4 and
 * TMC programming so it can be reused across Cortex-M SoCs.
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <zephyr/debug/coresight/etm.h>
#include <cmsis_core.h>

/*
 * The vendor SoC headers pulled in via cmsis_core.h may define
 * ETR_MODE_MODE_CIRCULARBUF for their own ETR peripheral; drop it so the generic
 * CoreSight definition from coresight_arm.h is used instead.
 */
#undef ETR_MODE_MODE_CIRCULARBUF

#include "coresight_arm.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etm_trace, CONFIG_ETM_TRACE_LOG_LEVEL);

#define ETM_NODE DT_NODELABEL(etm)
BUILD_ASSERT(DT_NODE_EXISTS(ETM_NODE),
	     "An 'etm' devicetree node label (compatible arm,coresight-etm4x) is required");

#define ETM_BASE     ((mem_addr_t)DT_REG_ADDR(ETM_NODE))
#define ETM_TRACE_ID DT_PROP(ETM_NODE, arm_trace_id)

#if defined(CONFIG_ETM_TRACE_SINK_ETB)
#define SINK_NODE DT_NODELABEL(etb)
BUILD_ASSERT(DT_NODE_EXISTS(SINK_NODE), "ETM ETB sink requires an 'etb' devicetree node label");
#elif defined(CONFIG_ETM_TRACE_SINK_ETR)
#define SINK_NODE     DT_NODELABEL(etr)
#define SINK_BUF_NODE DT_NODELABEL(etr_buffer)
BUILD_ASSERT(DT_NODE_EXISTS(SINK_NODE), "ETM ETR sink requires an 'etr' devicetree node label");
BUILD_ASSERT(DT_NODE_EXISTS(SINK_BUF_NODE),
	     "ETM ETR sink requires an 'etr_buffer' devicetree node label");
#endif

#define SINK_BASE ((mem_addr_t)DT_REG_ADDR(SINK_NODE))

/* Bound for register handshake polling loops. */
#define ETM_POLL_TIMEOUT_US 1000

static bool tracing_active;

static int poll_bit_set(mem_addr_t reg, uint32_t mask)
{
	for (uint32_t i = 0; i < ETM_POLL_TIMEOUT_US; i++) {
		if ((sys_read32(reg) & mask) != 0) {
			return 0;
		}
		k_busy_wait(1);
	}

	return -EIO;
}

static int poll_bit_clear(mem_addr_t reg, uint32_t mask)
{
	for (uint32_t i = 0; i < ETM_POLL_TIMEOUT_US; i++) {
		if ((sys_read32(reg) & mask) == 0) {
			return 0;
		}
		k_busy_wait(1);
	}

	return -EIO;
}

static int etm_source_start(void)
{
	int err;
	uint32_t configr = 0;

	/* Enable the Cortex-M trace subsystem, required to access ETM registers. */
	DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
	__DSB();
	__ISB();

	err = coresight_unlock(ETM_BASE);
	if (err < 0) {
		LOG_ERR("Failed to unlock ETM");
		return err;
	}

	/* Request and wait for ETM power so the trace registers are accessible. */
	sys_write32(CORESIGHT_ETM_TRCPDCR_PU_Msk, ETM_BASE + CORESIGHT_ETM_TRCPDCR_OFFSET);
	err = poll_bit_set(ETM_BASE + CORESIGHT_ETM_TRCPDSR_OFFSET, CORESIGHT_ETM_TRCPDSR_POWER_Msk);
	if (err < 0) {
		LOG_ERR("ETM did not power up");
		return err;
	}

	/* Clear the OS Lock so the ETM can be programmed. */
	sys_write32(CORESIGHT_ETM_TRCOSLAR_UNLOCK, ETM_BASE + CORESIGHT_ETM_TRCOSLAR_OFFSET);

	/* Disable the ETM and wait until it is idle before reprogramming it. */
	sys_write32(0, ETM_BASE + CORESIGHT_ETM_TRCPRGCTLR_OFFSET);
	err = poll_bit_set(ETM_BASE + CORESIGHT_ETM_TRCSTATR_OFFSET, CORESIGHT_ETM_TRCSTATR_IDLE_Msk);
	if (err < 0) {
		LOG_ERR("ETM did not become idle");
		return err;
	}

	if (IS_ENABLED(CONFIG_ETM_TRACE_BRANCH_BROADCAST)) {
		configr |= CORESIGHT_ETM_TRCCONFIGR_BB_Msk;
		sys_write32(CORESIGHT_ETM_TRCBBCTLR_BROADCAST_ALL, ETM_BASE + CORESIGHT_ETM_TRCBBCTLR_OFFSET);
	}
	if (IS_ENABLED(CONFIG_ETM_TRACE_TIMESTAMPS)) {
		configr |= CORESIGHT_ETM_TRCCONFIGR_TS_Msk;
	}

	sys_write32(configr, ETM_BASE + CORESIGHT_ETM_TRCCONFIGR_OFFSET);
	sys_write32(0, ETM_BASE + CORESIGHT_ETM_TRCEVENTCTL0R_OFFSET);
	sys_write32(0, ETM_BASE + CORESIGHT_ETM_TRCEVENTCTL1R_OFFSET);
	sys_write32(0, ETM_BASE + CORESIGHT_ETM_TRCTSCTLR_OFFSET);
	sys_write32(CONFIG_ETM_TRACE_SYNC_PERIOD, ETM_BASE + CORESIGHT_ETM_TRCSYNCPR_OFFSET);
	sys_write32(ETM_TRACE_ID, ETM_BASE + CORESIGHT_ETM_TRCTRACEIDR_OFFSET);

	/* Trace the whole instruction stream unconditionally. */
	sys_write32(CORESIGHT_ETM_TRCVICTLR_TRACE_ALL, ETM_BASE + CORESIGHT_ETM_TRCVICTLR_OFFSET);

	/* Enable the ETM and wait until it leaves the idle state. */
	sys_write32(CORESIGHT_ETM_TRCPRGCTLR_EN_Msk, ETM_BASE + CORESIGHT_ETM_TRCPRGCTLR_OFFSET);
	err = poll_bit_clear(ETM_BASE + CORESIGHT_ETM_TRCSTATR_OFFSET, CORESIGHT_ETM_TRCSTATR_IDLE_Msk);
	if (err < 0) {
		LOG_ERR("ETM did not start tracing");
		return err;
	}

	return 0;
}

static int etm_source_stop(void)
{
	int err;

	/* Disable the ETM and wait until it has drained and become idle. */
	sys_write32(0, ETM_BASE + CORESIGHT_ETM_TRCPRGCTLR_OFFSET);
	err = poll_bit_set(ETM_BASE + CORESIGHT_ETM_TRCSTATR_OFFSET, CORESIGHT_ETM_TRCSTATR_IDLE_Msk);
	if (err < 0) {
		LOG_ERR("ETM did not stop");
		return err;
	}

	return 0;
}

static int sink_start(void)
{
	int err;

	err = coresight_unlock(SINK_BASE);
	if (err < 0) {
		LOG_ERR("Failed to unlock trace sink");
		return err;
	}

	/* Make sure capture is disabled while (re)configuring the sink. */
	sys_write32(0, SINK_BASE + TMC_CTL_OFFSET);

#if defined(CONFIG_ETM_TRACE_SINK_ETR)
	uintptr_t buf = (uintptr_t)DT_REG_ADDR(SINK_BUF_NODE);
	size_t buf_words = DT_REG_SIZE(SINK_BUF_NODE) / sizeof(uint32_t);

	sys_write32(buf_words, SINK_BASE + TMC_RSZ_OFFSET);
	sys_write32(buf, SINK_BASE + TMC_RWP_OFFSET);
	sys_write32(buf, SINK_BASE + TMC_DBALO_OFFSET);
	sys_write32(0, SINK_BASE + TMC_DBAHI_OFFSET);
#endif

	sys_write32(TMC_FFCR_ENFT_Msk, SINK_BASE + TMC_FFCR_OFFSET);
	sys_write32(TMC_MODE_CIRCULAR_BUFFER, SINK_BASE + TMC_MODE_OFFSET);
	sys_write32(TMC_CTL_TRACECAPTEN_Msk, SINK_BASE + TMC_CTL_OFFSET);

	return 0;
}

static int sink_stop(void)
{
	int err;

	/* Manually flush the formatter and wait for the sink to drain. */
	sys_write32(TMC_FFCR_ENFT_Msk | TMC_FFCR_FLUSHMAN_Msk, SINK_BASE + TMC_FFCR_OFFSET);
	err = poll_bit_clear(SINK_BASE + TMC_FFCR_OFFSET, TMC_FFCR_FLUSHMAN_Msk);
	if (err < 0) {
		LOG_WRN("Trace sink flush did not complete");
	}

	err = poll_bit_set(SINK_BASE + TMC_STS_OFFSET, TMC_STS_TMCREADY_Msk);
	if (err < 0) {
		LOG_WRN("Trace sink did not become ready");
	}

	/* Stop capturing. */
	sys_write32(0, SINK_BASE + TMC_CTL_OFFSET);

	return 0;
}

int etm_trace_start(void)
{
	int err;

	if (tracing_active) {
		return 0;
	}

	err = sink_start();
	if (err < 0) {
		return err;
	}

	err = etm_source_start();
	if (err < 0) {
		return err;
	}

	tracing_active = true;
	LOG_DBG("ETM tracing started (trace ID 0x%02x)", ETM_TRACE_ID);

	return 0;
}

int etm_trace_stop(void)
{
	int err;

	if (!tracing_active) {
		return 0;
	}

	err = etm_source_stop();
	if (err < 0) {
		return err;
	}

	err = sink_stop();
	if (err < 0) {
		return err;
	}

	tracing_active = false;
	LOG_DBG("ETM tracing stopped, %zu bytes captured", etm_trace_get_size());

	return 0;
}

size_t etm_trace_get_size(void)
{
	bool full = (sys_read32(SINK_BASE + TMC_STS_OFFSET) & TMC_STS_FULL_Msk) != 0;

#if defined(CONFIG_ETM_TRACE_SINK_ETR)
	if (full) {
		return DT_REG_SIZE(SINK_BUF_NODE);
	}

	uintptr_t buf = (uintptr_t)DT_REG_ADDR(SINK_BUF_NODE);

	return (size_t)(sys_read32(SINK_BASE + TMC_RWP_OFFSET) - buf);
#else
	if (full) {
		return sys_read32(SINK_BASE + TMC_RSZ_OFFSET) * sizeof(uint32_t);
	}

	return sys_read32(SINK_BASE + TMC_RWP_OFFSET);
#endif
}

ssize_t etm_trace_read(uint8_t *buf, size_t len)
{
	if (buf == NULL) {
		return -EINVAL;
	}

	size_t avail = etm_trace_get_size();
	size_t to_copy = MIN(len, avail);

#if defined(CONFIG_ETM_TRACE_SINK_ETR)
	/*
	 * ETR captured the stream straight into system RAM. When the buffer has
	 * wrapped the oldest data starts at the write pointer, otherwise it starts
	 * at the buffer base.
	 */
	uintptr_t base = (uintptr_t)DT_REG_ADDR(SINK_BUF_NODE);
	size_t size = DT_REG_SIZE(SINK_BUF_NODE);
	bool full = (sys_read32(SINK_BASE + TMC_STS_OFFSET) & TMC_STS_FULL_Msk) != 0;
	size_t start = full ? (size_t)(sys_read32(SINK_BASE + TMC_RWP_OFFSET) - base) : 0;

	for (size_t i = 0; i < to_copy; i++) {
		buf[i] = ((const uint8_t *)base)[(start + i) % size];
	}

	return (ssize_t)to_copy;
#else
	/*
	 * Drain the ETB/ETF FIFO through the RAM Read Data register. Point the read
	 * pointer at the oldest data first.
	 */
	bool full = (sys_read32(SINK_BASE + TMC_STS_OFFSET) & TMC_STS_FULL_Msk) != 0;

	sys_write32(full ? sys_read32(SINK_BASE + TMC_RWP_OFFSET) : 0,
		    SINK_BASE + TMC_RRP_OFFSET);

	size_t copied = 0;

	while (copied + sizeof(uint32_t) <= to_copy) {
		uint32_t word = sys_read32(SINK_BASE + TMC_RRD_OFFSET);

		if (word == TMC_RRD_DRAINED) {
			break;
		}

		memcpy(&buf[copied], &word, sizeof(word));
		copied += sizeof(word);
	}

	return (ssize_t)copied;
#endif
}
