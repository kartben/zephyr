/*
 * Copyright (c) 2018 Linaro Limited.
 * Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ARM AArch32 specific syscall header
 *
 * This header contains the ARM AArch32 specific syscall interface. It is
 * included by the syscall interface architecture-abstraction header
 * (include/arch/syscall.h)
 */

#ifndef ZEPHYR_INCLUDE_ARCH_ARM_SYSCALL_H_
#define ZEPHYR_INCLUDE_ARCH_ARM_SYSCALL_H_

/** @brief SVC call ID for context switch */
#define _SVC_CALL_CONTEXT_SWITCH	0
/** @brief SVC call ID for IRQ offload */
#define _SVC_CALL_IRQ_OFFLOAD		1
/** @brief SVC call ID for runtime exception */
#define _SVC_CALL_RUNTIME_EXCEPT	2
/** @brief SVC call ID for system call */
#define _SVC_CALL_SYSTEM_CALL		3

#ifdef CONFIG_USERSPACE
#ifndef _ASMLANGUAGE

#include <zephyr/types.h>
#include <stdbool.h>
#include <zephyr/arch/arm/misc.h>
#include <zephyr/sys/util_macro.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Invoke a system call with 6 arguments
 *
 * Performs an ARM SVC instruction to invoke a system call from user mode.
 * Uses ARM-specific machine constraints to ensure arguments are placed
 * in the proper registers (r0-r5 for args, r6 for call_id).
 *
 * @param arg1 First argument to the system call.
 * @param arg2 Second argument to the system call.
 * @param arg3 Third argument to the system call.
 * @param arg4 Fourth argument to the system call.
 * @param arg5 Fifth argument to the system call.
 * @param arg6 Sixth argument to the system call.
 * @param call_id The system call identifier.
 *
 * @return The return value from the system call.
 */
static inline uintptr_t arch_syscall_invoke6(uintptr_t arg1, uintptr_t arg2,
					     uintptr_t arg3, uintptr_t arg4,
					     uintptr_t arg5, uintptr_t arg6,
					     uintptr_t call_id)
{
	register uint32_t ret __asm__("r0") = arg1;
	register uint32_t r1 __asm__("r1") = arg2;
	register uint32_t r2 __asm__("r2") = arg3;
	register uint32_t r3 __asm__("r3") = arg4;
	register uint32_t r4 __asm__("r4") = arg5;
	register uint32_t r5 __asm__("r5") = arg6;
	register uint32_t r6 __asm__("r6") = call_id;

	__asm__ volatile("svc %[svid]\n"
			 IF_ENABLED(CONFIG_ARM_BTI, ("bti\n"))
			 : "=r"(ret), "=r"(r1), "=r"(r2), "=r"(r3)
			 : [svid] "i" (_SVC_CALL_SYSTEM_CALL),
			   "r" (ret), "r" (r1), "r" (r2), "r" (r3),
			   "r" (r4), "r" (r5), "r" (r6)
			 : "r8", "memory", "ip");

	return ret;
}

/**
 * @brief Invoke a system call with 5 arguments
 *
 * Performs an ARM SVC instruction to invoke a system call from user mode.
 *
 * @param arg1 First argument to the system call.
 * @param arg2 Second argument to the system call.
 * @param arg3 Third argument to the system call.
 * @param arg4 Fourth argument to the system call.
 * @param arg5 Fifth argument to the system call.
 * @param call_id The system call identifier.
 *
 * @return The return value from the system call.
 */
static inline uintptr_t arch_syscall_invoke5(uintptr_t arg1, uintptr_t arg2,
					     uintptr_t arg3, uintptr_t arg4,
					     uintptr_t arg5,
					     uintptr_t call_id)
{
	register uint32_t ret __asm__("r0") = arg1;
	register uint32_t r1 __asm__("r1") = arg2;
	register uint32_t r2 __asm__("r2") = arg3;
	register uint32_t r3 __asm__("r3") = arg4;
	register uint32_t r4 __asm__("r4") = arg5;
	register uint32_t r6 __asm__("r6") = call_id;

	__asm__ volatile("svc %[svid]\n"
			 IF_ENABLED(CONFIG_ARM_BTI, ("bti\n"))
			 : "=r"(ret), "=r"(r1), "=r"(r2), "=r"(r3)
			 : [svid] "i" (_SVC_CALL_SYSTEM_CALL),
			   "r" (ret), "r" (r1), "r" (r2), "r" (r3),
			   "r" (r4), "r" (r6)
			 : "r8", "memory", "ip");

	return ret;
}

/**
 * @brief Invoke a system call with 4 arguments
 *
 * Performs an ARM SVC instruction to invoke a system call from user mode.
 *
 * @param arg1 First argument to the system call.
 * @param arg2 Second argument to the system call.
 * @param arg3 Third argument to the system call.
 * @param arg4 Fourth argument to the system call.
 * @param call_id The system call identifier.
 *
 * @return The return value from the system call.
 */
static inline uintptr_t arch_syscall_invoke4(uintptr_t arg1, uintptr_t arg2,
					     uintptr_t arg3, uintptr_t arg4,
					     uintptr_t call_id)
{
	register uint32_t ret __asm__("r0") = arg1;
	register uint32_t r1 __asm__("r1") = arg2;
	register uint32_t r2 __asm__("r2") = arg3;
	register uint32_t r3 __asm__("r3") = arg4;
	register uint32_t r6 __asm__("r6") = call_id;

	__asm__ volatile("svc %[svid]\n"
			 IF_ENABLED(CONFIG_ARM_BTI, ("bti\n"))
			 : "=r"(ret), "=r"(r1), "=r"(r2), "=r"(r3)
			 : [svid] "i" (_SVC_CALL_SYSTEM_CALL),
			   "r" (ret), "r" (r1), "r" (r2), "r" (r3),
			   "r" (r6)
			 : "r8", "memory", "ip");

	return ret;
}

/**
 * @brief Invoke a system call with 3 arguments
 *
 * Performs an ARM SVC instruction to invoke a system call from user mode.
 *
 * @param arg1 First argument to the system call.
 * @param arg2 Second argument to the system call.
 * @param arg3 Third argument to the system call.
 * @param call_id The system call identifier.
 *
 * @return The return value from the system call.
 */
static inline uintptr_t arch_syscall_invoke3(uintptr_t arg1, uintptr_t arg2,
					     uintptr_t arg3,
					     uintptr_t call_id)
{
	register uint32_t ret __asm__("r0") = arg1;
	register uint32_t r1 __asm__("r1") = arg2;
	register uint32_t r2 __asm__("r2") = arg3;
	register uint32_t r6 __asm__("r6") = call_id;

	__asm__ volatile("svc %[svid]\n"
			 IF_ENABLED(CONFIG_ARM_BTI, ("bti\n"))
			 : "=r"(ret), "=r"(r1), "=r"(r2)
			 : [svid] "i" (_SVC_CALL_SYSTEM_CALL),
			   "r" (ret), "r" (r1), "r" (r2), "r" (r6)
			 : "r8", "memory", "r3", "ip");

	return ret;
}

/**
 * @brief Invoke a system call with 2 arguments
 *
 * Performs an ARM SVC instruction to invoke a system call from user mode.
 *
 * @param arg1 First argument to the system call.
 * @param arg2 Second argument to the system call.
 * @param call_id The system call identifier.
 *
 * @return The return value from the system call.
 */
static inline uintptr_t arch_syscall_invoke2(uintptr_t arg1, uintptr_t arg2,
					     uintptr_t call_id)
{
	register uint32_t ret __asm__("r0") = arg1;
	register uint32_t r1 __asm__("r1") = arg2;
	register uint32_t r6 __asm__("r6") = call_id;

	__asm__ volatile("svc %[svid]\n"
			 IF_ENABLED(CONFIG_ARM_BTI, ("bti\n"))
			 : "=r"(ret), "=r"(r1)
			 : [svid] "i" (_SVC_CALL_SYSTEM_CALL),
			   "r" (ret), "r" (r1), "r" (r6)
			 : "r8", "memory", "r2", "r3", "ip");

	return ret;
}

/**
 * @brief Invoke a system call with 1 argument
 *
 * Performs an ARM SVC instruction to invoke a system call from user mode.
 *
 * @param arg1 First argument to the system call.
 * @param call_id The system call identifier.
 *
 * @return The return value from the system call.
 */
static inline uintptr_t arch_syscall_invoke1(uintptr_t arg1,
					     uintptr_t call_id)
{
	register uint32_t ret __asm__("r0") = arg1;
	register uint32_t r6 __asm__("r6") = call_id;

	__asm__ volatile("svc %[svid]\n"
			 IF_ENABLED(CONFIG_ARM_BTI, ("bti\n"))
			 : "=r"(ret)
			 : [svid] "i" (_SVC_CALL_SYSTEM_CALL),
			   "r" (ret), "r" (r6)
			 : "r8", "memory", "r1", "r2", "r3", "ip");
	return ret;
}

/**
 * @brief Invoke a system call with no arguments
 *
 * Performs an ARM SVC instruction to invoke a system call from user mode.
 *
 * @param call_id The system call identifier.
 *
 * @return The return value from the system call.
 */
static inline uintptr_t arch_syscall_invoke0(uintptr_t call_id)
{
	register uint32_t ret __asm__("r0");
	register uint32_t r6 __asm__("r6") = call_id;

	__asm__ volatile("svc %[svid]\n"
			 IF_ENABLED(CONFIG_ARM_BTI, ("bti\n"))
			 : "=r"(ret)
			 : [svid] "i" (_SVC_CALL_SYSTEM_CALL),
			   "r" (ret), "r" (r6)
			 : "r8", "memory", "r1", "r2", "r3", "ip");

	return ret;
}

/**
 * @brief Check if the current execution context is user mode
 *
 * Determines whether the calling code is running in user (unprivileged)
 * mode. On Cortex-M, this first checks if we're in handler mode (which
 * is always privileged), then checks the thread's privilege level.
 *
 * @return true if running in user context, false if in privileged context.
 */
static inline bool arch_is_user_context(void)
{
#if defined(CONFIG_CPU_CORTEX_M)
	uint32_t value;

	/* check for handler mode */
	__asm__ volatile("mrs %0, IPSR\n\t" : "=r"(value));
	if (value) {
		return false;
	}
#endif

	return z_arm_thread_is_in_user_mode();
}

#ifdef __cplusplus
}
#endif

#endif /* _ASMLANGUAGE */
#endif /* CONFIG_USERSPACE */
#endif /* ZEPHYR_INCLUDE_ARCH_ARM_SYSCALL_H_ */
