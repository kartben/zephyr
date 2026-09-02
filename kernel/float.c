/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 * Copyright (c) 2024 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/internal/syscall_handler.h>
#include <kernel_arch_interface.h>

int z_impl_k_float_disable(struct k_thread *thread)
{
#if defined(CONFIG_FPU) && defined(CONFIG_FPU_SHARING)
	return arch_float_disable(thread);
#else
	ARG_UNUSED(thread);
	return -ENOTSUP;
#endif /* CONFIG_FPU && CONFIG_FPU_SHARING */
}

int z_impl_k_float_enable(struct k_thread *thread, unsigned int options)
{
#if defined(CONFIG_FPU) && defined(CONFIG_FPU_SHARING)
	return arch_float_enable(thread, options);
#else
	ARG_UNUSED(thread);
	ARG_UNUSED(options);
	return -ENOTSUP;
#endif /* CONFIG_FPU && CONFIG_FPU_SHARING */
}

#ifdef CONFIG_USERSPACE
static inline int z_vrfy_k_float_disable(struct k_thread *thread)
{
	K_OOPS(K_SYSCALL_OBJ(thread, K_OBJ_THREAD));
	return z_impl_k_float_disable(thread);
}
#include <zephyr/syscalls/k_float_disable_mrsh.c>

static inline int z_vrfy_k_float_enable(struct k_thread *thread, unsigned int options)
{
	K_OOPS(K_SYSCALL_OBJ(thread, K_OBJ_THREAD));
	/* The architecture code ORs options into thread->base.user_options,
	 * so only the floating point option bits may come from user mode.
	 */
	K_OOPS(K_SYSCALL_VERIFY_MSG((options & ~(K_FP_REGS | K_SSE_REGS)) == 0U,
				    "invalid FP options 0x%x", options));
	return z_impl_k_float_enable(thread, options);
}
#include <zephyr/syscalls/k_float_enable_mrsh.c>

#endif /* CONFIG_USERSPACE */
