/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NSI_COMMON_SRC_INCL_HWS_MODELS_IF_H
#define NSI_COMMON_SRC_INCL_HWS_MODELS_IF_H

#include <stdint.h>
#include "nsi_utils.h"
#include "nsi_hw_scheduler.h"
#include "nsi_host_sections.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Internal structure used to link HW events */
struct nsi_hw_event_st {
	void (*const callback)(void);
	uint64_t *timer;
#if defined(__APPLE__)
	/*
	 * On macOS the event priority cannot be encoded in (and sorted by) the
	 * section name, so it is carried here and the events are ordered by it at
	 * runtime by the HW scheduler.
	 */
	uint16_t prio;
#endif
};

/**
 * Register an event timer and event callback
 *
 * The HW scheduler will keep track of this event, and call its callback whenever its
 * timer is reached.
 * The ordering of events in the same microsecond is given by prio (lowest first).
 * (Normally HW models will not care about the event ordering, and will simply set a prio like 100)
 *
 * Only very particular models will need to execute before or after others.
 *
 * Priority can be a number between 0 and 999.
 */
#if defined(__APPLE__)

#define NSI_HW_EVENT(t, fn, prio)					\
	static const struct nsi_hw_event_st NSI_CONCAT(NSI_CONCAT(__nsi_hw_event_, fn), t) \
		NSI_HOST_SECTION("__nsi_hwev") NSI_NOASAN				\
		= {			\
			.callback = fn,	\
			.timer = &t,	\
			.prio = prio,	\
		}

#else

#define NSI_HW_EVENT(t, fn, prio)					\
	static const struct nsi_hw_event_st NSI_CONCAT(NSI_CONCAT(__nsi_hw_event_, fn), t) \
		__attribute__((__used__)) NSI_NOASAN					\
		__attribute__((__section__(".nsi_hw_event_" NSI_STRINGIFY(prio))))	\
		= {			\
			.callback = fn,	\
			.timer = &t,	\
		}

#endif /* defined(__APPLE__) */

#ifdef __cplusplus
}
#endif

#endif /* NSI_COMMON_SRC_INCL_HWS_MODELS_IF_H */
