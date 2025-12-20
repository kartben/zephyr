/*
 * Copyright (c) 2013-2014 Wind River Systems, Inc.
 * Copyright (c) 2019 Nordic Semiconductor ASA.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ARM AArch32 public interrupt handling
 *
 * ARM AArch32-specific kernel interrupt handling interface. Included by
 * arm/arch.h. This header provides functions for enabling, disabling,
 * and managing interrupt priorities, as well as macros for connecting
 * interrupt service routines.
 */

#ifndef ZEPHYR_INCLUDE_ARCH_ARM_IRQ_H_
#define ZEPHYR_INCLUDE_ARCH_ARM_IRQ_H_

#include <zephyr/sw_isr_table.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _ASMLANGUAGE
GTEXT(z_arm_int_exit);
GTEXT(arch_irq_enable)
GTEXT(arch_irq_disable)
GTEXT(arch_irq_is_enabled)
#if defined(CONFIG_ARM_CUSTOM_INTERRUPT_CONTROLLER)
GTEXT(z_soc_irq_get_active)
GTEXT(z_soc_irq_eoi)
#endif /* CONFIG_ARM_CUSTOM_INTERRUPT_CONTROLLER */
#else

#if !defined(CONFIG_ARM_CUSTOM_INTERRUPT_CONTROLLER)
/**
 * @brief Enable an interrupt line
 *
 * @param irq IRQ line number to enable.
 */
extern void arm_irq_enable(unsigned int irq);

/**
 * @brief Disable an interrupt line
 *
 * @param irq IRQ line number to disable.
 */
extern void arm_irq_disable(unsigned int irq);

/**
 * @brief Check if an interrupt line is enabled
 *
 * @param irq IRQ line number to check.
 *
 * @return Non-zero if the interrupt is enabled, zero otherwise.
 */
extern int arm_irq_is_enabled(unsigned int irq);

/**
 * @brief Set the priority of an interrupt
 *
 * @param irq IRQ line number.
 * @param prio Priority level to set.
 * @param flags Architecture-specific flags (e.g., IRQ_ZERO_LATENCY).
 */
extern void arm_irq_priority_set(unsigned int irq, unsigned int prio, uint32_t flags);
#if !defined(CONFIG_MULTI_LEVEL_INTERRUPTS)
#define arch_irq_enable(irq)                     arm_irq_enable(irq)
#define arch_irq_disable(irq)                    arm_irq_disable(irq)
#define arch_irq_is_enabled(irq)                 arm_irq_is_enabled(irq)
#define z_arm_irq_priority_set(irq, prio, flags) arm_irq_priority_set(irq, prio, flags)
#endif
#endif

#if defined(CONFIG_ARM_CUSTOM_INTERRUPT_CONTROLLER) || defined(CONFIG_MULTI_LEVEL_INTERRUPTS)
/*
 * When a custom interrupt controller or multi-level interrupts is specified,
 * map the architecture interrupt control functions to the SoC layer interrupt
 * control functions.
 */

/**
 * @brief Initialize SoC-specific interrupt handling
 */
void z_soc_irq_init(void);

/**
 * @brief SoC-specific IRQ enable
 *
 * @param irq IRQ line number to enable.
 */
void z_soc_irq_enable(unsigned int irq);

/**
 * @brief SoC-specific IRQ disable
 *
 * @param irq IRQ line number to disable.
 */
void z_soc_irq_disable(unsigned int irq);

/**
 * @brief Check if a SoC-specific IRQ is enabled
 *
 * @param irq IRQ line number to check.
 *
 * @return Non-zero if the interrupt is enabled, zero otherwise.
 */
int z_soc_irq_is_enabled(unsigned int irq);

/**
 * @brief Set the priority of a SoC-specific IRQ
 *
 * @param irq IRQ line number.
 * @param prio Priority level to set.
 * @param flags Architecture-specific flags.
 */
void z_soc_irq_priority_set(
	unsigned int irq, unsigned int prio, unsigned int flags);

/**
 * @brief Get the currently active IRQ number
 *
 * @return The IRQ number of the currently active interrupt.
 */
unsigned int z_soc_irq_get_active(void);

/**
 * @brief Signal end of interrupt processing
 *
 * @param irq IRQ line number that has been serviced.
 */
void z_soc_irq_eoi(unsigned int irq);

#define arch_irq_enable(irq)		z_soc_irq_enable(irq)
#define arch_irq_disable(irq)		z_soc_irq_disable(irq)
#define arch_irq_is_enabled(irq)	z_soc_irq_is_enabled(irq)

#define z_arm_irq_priority_set(irq, prio, flags)	\
	z_soc_irq_priority_set(irq, prio, flags)

#endif

/**
 * @brief Exit from an interrupt handler
 *
 * This function is called at the end of interrupt processing to potentially
 * trigger a context switch if a higher priority thread has become ready.
 */
extern void z_arm_int_exit(void);

/**
 * @brief Initialize ARM interrupt handling
 *
 * Performs architecture-specific initialization of the interrupt subsystem.
 */
extern void z_arm_interrupt_init(void);

/**
 * @brief Flag to configure an interrupt as zero-latency
 *
 * Set this interrupt up as a zero-latency IRQ. If CONFIG_ZERO_LATENCY_LEVELS
 * is 1 it has a fixed hardware priority level (discarding what was supplied
 * in the interrupt's priority argument). If CONFIG_ZERO_LATENCY_LEVELS is
 * greater 1 it has the priority level assigned by the argument.
 * The interrupt will run even if irq_lock() is active. Be careful!
 */
#define IRQ_ZERO_LATENCY	BIT(0)

#ifdef CONFIG_CPU_CORTEX_M

#if defined(CONFIG_ZERO_LATENCY_LEVELS)
#define ZERO_LATENCY_LEVELS CONFIG_ZERO_LATENCY_LEVELS
#else
#define ZERO_LATENCY_LEVELS 1
#endif

#define _CHECK_PRIO(priority_p, flags_p) \
	BUILD_ASSERT(((flags_p & IRQ_ZERO_LATENCY) && \
		      ((ZERO_LATENCY_LEVELS == 1) || \
		       (priority_p < ZERO_LATENCY_LEVELS))) || \
		     (priority_p <= IRQ_PRIO_LOWEST), \
		     "Invalid interrupt priority. Values must not exceed IRQ_PRIO_LOWEST");
#else
#define _CHECK_PRIO(priority_p, flags_p)
#endif

/**
 * @brief Connect an interrupt handler to an IRQ line (ARM implementation)
 *
 * All arguments must be computable by the compiler at build time.
 * Z_ISR_DECLARE will populate the .intList section with the interrupt's
 * parameters, which will then be used by gen_irq_tables.py to create
 * the vector table and the software ISR table. This is all done at
 * build-time. The priority is additionally set in the interrupt controller
 * at runtime.
 */
#define ARCH_IRQ_CONNECT(irq_p, priority_p, isr_p, isr_param_p, flags_p) \
{ \
	BUILD_ASSERT(!(flags_p & IRQ_ZERO_LATENCY), \
			"ZLI interrupts must be registered using IRQ_DIRECT_CONNECT()"); \
	_CHECK_PRIO(priority_p, flags_p) \
	Z_ISR_DECLARE(irq_p, 0, isr_p, isr_param_p); \
	z_arm_irq_priority_set(irq_p, priority_p, flags_p); \
}

/**
 * @brief Directly connect an interrupt handler to an IRQ line (ARM implementation)
 *
 * Connects a direct interrupt handler, which has lower overhead than
 * regular interrupt handlers. Direct interrupts are suitable for
 * performance-critical interrupt handling.
 */
#define ARCH_IRQ_DIRECT_CONNECT(irq_p, priority_p, isr_p, flags_p) \
{ \
	BUILD_ASSERT(IS_ENABLED(CONFIG_ZERO_LATENCY_IRQS) || !(flags_p & IRQ_ZERO_LATENCY), \
			"ZLI interrupt registered but feature is disabled"); \
	_CHECK_PRIO(priority_p, flags_p) \
	Z_ISR_DECLARE_DIRECT(irq_p, ISR_FLAG_DIRECT, isr_p); \
	z_arm_irq_priority_set(irq_p, priority_p, flags_p); \
}

#ifdef CONFIG_PM
extern void _arch_isr_direct_pm(void);
/** @brief Power management hook for direct ISRs */
#define ARCH_ISR_DIRECT_PM() _arch_isr_direct_pm()
#else
/** @brief Power management hook for direct ISRs (no-op when PM disabled) */
#define ARCH_ISR_DIRECT_PM() do { } while (false)
#endif

/** @brief Direct ISR header macro */
#define ARCH_ISR_DIRECT_HEADER() arch_isr_direct_header()
/** @brief Direct ISR footer macro */
#define ARCH_ISR_DIRECT_FOOTER(swap) arch_isr_direct_footer(swap)

/* arch/arm/core/exc_exit.S */
extern void z_arm_int_exit(void);

#ifdef CONFIG_TRACING_ISR
extern void sys_trace_isr_enter(void);
extern void sys_trace_isr_exit(void);
#endif

/**
 * @brief Direct ISR entry header
 *
 * Called at the beginning of a direct interrupt service routine to
 * perform any necessary bookkeeping such as tracing.
 */
static inline void arch_isr_direct_header(void)
{
#ifdef CONFIG_TRACING_ISR
	sys_trace_isr_enter();
#endif
}

/**
 * @brief Direct ISR exit footer
 *
 * Called at the end of a direct interrupt service routine to perform
 * any necessary cleanup and potentially trigger a context switch.
 *
 * @param maybe_swap Non-zero if a context switch should be considered.
 */
static inline void arch_isr_direct_footer(int maybe_swap)
{
#ifdef CONFIG_TRACING_ISR
	sys_trace_isr_exit();
#endif
	if (maybe_swap != 0) {
		z_arm_int_exit();
	}
}

/** @brief Disable warnings for ISR declaration */
#define ARCH_ISR_DIAG_OFF \
	TOOLCHAIN_DISABLE_CLANG_WARNING(TOOLCHAIN_WARNING_EXTRA) \
	TOOLCHAIN_DISABLE_GCC_WARNING(TOOLCHAIN_WARNING_ATTRIBUTES) \
	TOOLCHAIN_DISABLE_IAR_WARNING(TOOLCHAIN_WARNING_ATTRIBUTES)
/** @brief Re-enable warnings after ISR declaration */
#define ARCH_ISR_DIAG_ON \
	TOOLCHAIN_ENABLE_CLANG_WARNING(TOOLCHAIN_WARNING_EXTRA) \
	TOOLCHAIN_ENABLE_GCC_WARNING(TOOLCHAIN_WARNING_ATTRIBUTES) \
	TOOLCHAIN_ENABLE_IAR_WARNING(TOOLCHAIN_WARNING_ATTRIBUTES)

/**
 * @brief Declare a direct interrupt service routine
 *
 * This macro creates the wrapper function for a direct ISR, handling
 * the entry/exit bookkeeping and context switch decision.
 */
#define ARCH_ISR_DIRECT_DECLARE(name) \
	static inline int name##_body(void); \
	ARCH_ISR_DIAG_OFF \
	__attribute__ ((interrupt ("IRQ"))) void name(void) \
	{ \
		int check_reschedule; \
		ISR_DIRECT_HEADER(); \
		check_reschedule = name##_body(); \
		ISR_DIRECT_FOOTER(check_reschedule); \
	} \
	ARCH_ISR_DIAG_ON \
	static inline int name##_body(void)

#if defined(CONFIG_DYNAMIC_DIRECT_INTERRUPTS)

/**
 * @brief Dynamic direct interrupt dispatcher with reschedule
 *
 * Dispatches a dynamically connected direct interrupt and triggers
 * rescheduling upon return.
 */
extern void z_arm_irq_direct_dynamic_dispatch_reschedule(void);

/**
 * @brief Dynamic direct interrupt dispatcher without reschedule
 *
 * Dispatches a dynamically connected direct interrupt without
 * triggering rescheduling upon return.
 */
extern void z_arm_irq_direct_dynamic_dispatch_no_reschedule(void);

/**
 * @brief Macro to register an ISR Dispatcher (with or without re-scheduling
 * request) for dynamic direct interrupts.
 *
 * This macro registers the ISR dispatcher function for dynamic direct
 * interrupts for a particular IRQ line, allowing the use of dynamic
 * direct ISRs in the kernel for that interrupt source.
 * The dispatcher function is invoked when the hardware
 * interrupt occurs and then triggers the (software) Interrupt Service Routine
 * (ISR) that is registered dynamically (i.e. at run-time) into the software
 * ISR table stored in SRAM. The ISR must be connected with
 * irq_connect_dynamic() and enabled via irq_enable() before the dynamic direct
 * interrupt can be serviced. This ISR dispatcher must be configured by the
 * user to trigger thread re-secheduling upon return, using the @param resch
 * parameter.
 *
 * These ISRs are designed for performance-critical interrupt handling and do
 * not go through all of the common interrupt handling code.
 *
 * With respect to their declaration, dynamic 'direct' interrupts are regular
 * Zephyr interrupts; their signature must match void isr(void* parameter), as,
 * unlike regular direct interrupts, they are not placed directly into the
 * ROM hardware vector table but instead they are installed in the software
 * ISR table.
 *
 * The major differences with regular Zephyr interrupts are the following:
 * - Similar to direct interrupts, the call into the OS to exit power
 *   management idle state is optional. Normal interrupts always do this
 *   before the ISR is run, but with dynamic direct ones when and if it runs
 *   is controlled by the placement of
 *   a ISR_DIRECT_PM() macro, or omitted entirely.
 * - Similar to direct interrupts, scheduling decisions are optional. Unlike
 *   direct interrupts, the decisions must be made at build time.
 *   They are controlled by @param resch to this macro.
 *
 * @warning
 * Just like with regular direct ISRs, any ISRs that serve IRQs configured with
 * the IRQ_ZERO_LATENCY flag must not use the ISR_DIRECT_PM() macro and must
 * return 0 (i.e. resch must be no_reschedule).
 *
 * @param irq_p IRQ line number.
 * @param priority_p Interrupt priority.
 * @param flags_p Architecture-specific IRQ configuration flags.
 * @param resch Set flag to 'reschedule' to request thread
 *              re-scheduling upon ISR function. Set flag
 *              'no_reschedule' to skip thread re-scheduling
 *              Must be 'no_reschedule' for zero-latency interrupts
 *
 * Note: the function is an ARM Cortex-M only API.
 *
 * @return Interrupt vector assigned to this interrupt.
 */
#define ARM_IRQ_DIRECT_DYNAMIC_CONNECT(irq_p, priority_p, flags_p, resch) \
	IRQ_DIRECT_CONNECT(irq_p, priority_p, \
		_CONCAT(z_arm_irq_direct_dynamic_dispatch_, resch), flags_p)

#endif /* CONFIG_DYNAMIC_DIRECT_INTERRUPTS */

#if defined(CONFIG_ARM_SECURE_FIRMWARE)
/**
 * @brief IRQ target security state
 *
 * Defines the target security state for an NVIC IRQ line when
 * ARM TrustZone is enabled.
 */
typedef enum {
	IRQ_TARGET_STATE_SECURE = 0,    /**< IRQ targets secure state */
	IRQ_TARGET_STATE_NON_SECURE     /**< IRQ targets non-secure state */
} irq_target_state_t;

#endif /* CONFIG_ARM_SECURE_FIRMWARE */

#endif /* _ASMLANGUAGE */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ARCH_ARM_IRQ_H_ */
