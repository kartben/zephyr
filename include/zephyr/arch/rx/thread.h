/*
 * Copyright (c) 2021 KT-Elektronik, Klaucke und Partner GmbH
 * Copyright (c) 2024 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_ARCH_RX_THREAD_H_
#define ZEPHYR_INCLUDE_ARCH_RX_THREAD_H_

#ifndef _ASMLANGUAGE
#include <zephyr/types.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

struct _callee_saved {
	/* No general purpose callee-saved registers. */
	EMPTY_STRUCT_PLACEHOLDER;
};

typedef struct _callee_saved _callee_saved_t;

struct _thread_arch {
	/* This architecture keeps no per-thread state. */
	EMPTY_STRUCT_PLACEHOLDER;
};

typedef struct _thread_arch _thread_arch_t;

#ifdef __cplusplus
}
#endif

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_ARCH_RX_THREAD_H_ */
