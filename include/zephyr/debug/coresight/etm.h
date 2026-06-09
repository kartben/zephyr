/*
 * Copyright (c) 2026 Benjamin Cabé
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DEBUG_CORESIGHT_ETM_H_
#define ZEPHYR_INCLUDE_DEBUG_CORESIGHT_ETM_H_

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Arm CoreSight ETM instruction trace API
 * @defgroup etm_trace Arm CoreSight ETM instruction trace
 * @ingroup coresight_apis
 * @{
 *
 * This API drives an Arm CoreSight Embedded Trace Macrocell (ETM) instruction
 * trace source and an on-chip trace sink (ETB or ETR). The trace captured in
 * the sink buffer is the raw, formatted CoreSight byte stream and is meant to
 * be decoded offline (for example with OpenCSD or ptm2human) against the
 * application ELF.
 *
 * The CoreSight trace fabric routing the ETM trace to the sink (ATB funnels,
 * replicators, trace power domain) is configured by the SoC's CoreSight driver
 * during system initialization. This API only programs the ETM source and
 * reads back the captured trace.
 */

/**
 * @brief Start instruction tracing.
 *
 * Programs and enables the ETM trace source. From this point the configured
 * trace sink captures the executed instruction stream.
 *
 * @retval 0 on success.
 * @retval -EIO if the ETM or trace sink could not be programmed.
 * @retval -ENODEV if no ETM source or trace sink is available.
 */
int etm_trace_start(void);

/**
 * @brief Stop instruction tracing.
 *
 * Disables the ETM trace source and flushes the trace sink so that all
 * outstanding trace bytes are committed to the sink buffer before it is read
 * with @ref etm_trace_read.
 *
 * @retval 0 on success.
 * @retval -EIO if the ETM or trace sink could not be stopped.
 */
int etm_trace_stop(void);

/**
 * @brief Get the number of captured trace bytes available to read.
 *
 * Only meaningful after @ref etm_trace_stop.
 *
 * @return Number of bytes currently held in the trace sink buffer.
 */
size_t etm_trace_get_size(void);

/**
 * @brief Read captured trace bytes from the sink buffer.
 *
 * Copies up to @p len bytes of the captured CoreSight trace stream into
 * @p buf. Should be called after @ref etm_trace_stop. For FIFO based sinks
 * (ETB/ETF) the data is consumed as it is read.
 *
 * @param buf Destination buffer.
 * @param len Size of @p buf in bytes.
 *
 * @retval >=0 number of bytes copied into @p buf.
 * @retval -EINVAL if @p buf is NULL.
 * @retval -ENODEV if no trace sink is available.
 */
ssize_t etm_trace_read(uint8_t *buf, size_t len);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DEBUG_CORESIGHT_ETM_H_ */
