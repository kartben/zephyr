/*
 * Copyright 2023 The ChromiumOS Authors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Stand-in for <zephyr/arch/cpu.h>: this test builds the it8xxx2 GPIO driver
 * on native_sim, so the arch dispatch header has to pick the POSIX arch.
 * Keep the include guard and the arch_interface.h include of the real header:
 * arch.h pulls in <zephyr/irq.h>, which needs the arch_irq_*() declarations
 * before the arch definitions themselves have been seen.
 */

#ifndef ZEPHYR_INCLUDE_ARCH_CPU_H_
#define ZEPHYR_INCLUDE_ARCH_CPU_H_

#include <chip_chipregs.h>
#include <zephyr/arch/arch_interface.h>
#include <zephyr/arch/posix/arch.h>

#endif /* ZEPHYR_INCLUDE_ARCH_CPU_H_ */
