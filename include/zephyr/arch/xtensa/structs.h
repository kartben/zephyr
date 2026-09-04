/*
 * Copyright (c) Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_ARCH_XTENSA_STRUCTS_H_
#define ZEPHYR_INCLUDE_ARCH_XTENSA_STRUCTS_H_

/* Per CPU architecture specifics */
struct _cpu_arch {
#if defined(CONFIG_XTENSA_LAZY_HIFI_SHARING)
	atomic_ptr_val_t hifi_owner; /* Owner of HiFi */
#if CONFIG_MP_MAX_NUM_CPUS > 1
	atomic_ptr_val_t save_hifi;  /* Save HiFi on IPI if match hifi_owner */
#endif
#else
	/* An empty struct is not valid ISO C, and it has size 0 in C but 1
	 * in C++. Carry a dummy so the struct is never empty.
	 */
	uint8_t dummy;
#endif
};

BUILD_ASSERT(sizeof(struct _cpu_arch) >= 1);

#endif /* ZEPHYR_INCLUDE_ARCH_XTENSA_STRUCTS_H_ */
