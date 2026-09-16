/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/sw_isr_table.h>
#include <cmsis_core.h>

#define PREEMPTIONS 256U

extern char arm_m_switch_restore_start, arm_m_switch_restore_end;

volatile uint32_t restore_test_armed;
void *volatile restore_test_handle;
volatile uint32_t *restore_test_pending;
uint32_t restore_test_irq_mask;

static K_SEM_DEFINE(start, 0, 1);
static K_SEM_DEFINE(interrupted, 0, 1);
static K_THREAD_STACK_DEFINE(preemptor_stack, 1024);
static struct k_thread preemptor;
static struct k_thread *victim;
static void *first_handle;
static uint32_t resume_pc;
static uint32_t count;

/* Pend an interrupt before the real restore unmasks it. This wrapper exists
 * only in the test image; production restore instructions are unchanged.
 */
__attribute__((naked)) void __wrap_arm_m_switch_restore(void)
{
	__asm__("ldr r1, =restore_test_armed;"
		"ldr r2, [r1];"
		"cbz r2, 1f;"
		"ldr r1, =restore_test_handle;"
		"str r4, [r1];"
		"ldr r1, =restore_test_pending;"
		"ldr r1, [r1];"
		"ldr r2, =restore_test_irq_mask;"
		"ldr r2, [r2];"
		"str r2, [r1];"
		"dsb;"
		"1: b __real_arm_m_switch_restore;");
}

static void interrupt_restore(const void *arg)
{
	uint32_t *frame = (uint32_t *)__get_PSP();
	uint32_t pc = frame[6];

	ARG_UNUSED(arg);
	if ((k_current_get() != victim) || (restore_test_armed == 0U)) {
		return;
	}
	if ((pc < (uint32_t)&arm_m_switch_restore_start) ||
	    (pc >= (uint32_t)&arm_m_switch_restore_end)) {
		return;
	}

	if (first_handle == NULL) {
		first_handle = restore_test_handle;
	}

	/* Model suspension at the later loads, including a partially completed
	 * LDM. Recovery must ignore both the stacked scratch registers and ICI
	 * state and return the untouched original handle. Always wake a higher
	 * priority thread, so the edited frame is discarded rather than resumed.
	 */
	frame[6] = resume_pc;
	frame[7] |= BIT(10);
	frame[0] = 0xdead0000U;
	frame[1] = 0xdead0001U;
	k_sem_give(&interrupted);
}

static void preempt(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	zassert_ok(k_sem_take(&start, K_FOREVER));
#ifdef CONFIG_FPU
	__asm__ volatile("vmov s16, %0" :: "r"(0xdeadbeefU) : "s16");
#endif
	restore_test_armed = 1U;
	for (count = 0U; count < PREEMPTIONS; count++) {
		zassert_ok(k_sem_take(&interrupted, K_FOREVER));
		if (victim->switch_handle != first_handle) {
			restore_test_armed = 0U;
			zassert_equal_ptr(victim->switch_handle, first_handle,
					 "interrupted restore allocated another frame");
		}
	}
	restore_test_armed = 0U;
}

ZTEST(arm_switch_restore, test_late_restore_reuses_frame)
{
	int32_t priority = k_thread_priority_get(k_current_get());

	int32_t test_irq;

	for (test_irq = CONFIG_NUM_IRQS - 1; test_irq >= 0; test_irq--) {
		if ((_sw_isr_table[test_irq].isr == z_irq_spurious) &&
		    (NVIC_GetEnableIRQ(test_irq) == 0U)) {
			NVIC_SetPendingIRQ(test_irq);
			bool implemented = NVIC_GetPendingIRQ(test_irq) != 0U;

			NVIC_ClearPendingIRQ(test_irq);
			if (implemented) {
				break;
			}
		}
	}
	zassert_true(test_irq >= 0, "no unused IRQ");
	zassert_equal(irq_connect_dynamic(test_irq, 0, interrupt_restore, NULL, 0), test_irq);
	restore_test_pending = &NVIC->ISPR[(uint32_t)test_irq / 32U];
	restore_test_irq_mask = BIT((uint32_t)test_irq % 32U);
	irq_enable(test_irq);
	victim = k_current_get();
	k_thread_priority_set(victim, K_PRIO_PREEMPT(1));

	/* The last three instructions are 32-bit loads of the callee-saved
	 * registers, caller-saved registers, and finally PC with SP writeback.
	 */
	for (uint32_t offset = 4U; offset <= 12U; offset += 4U) {
		first_handle = NULL;
		count = 0U;
		resume_pc = (uint32_t)&arm_m_switch_restore_end - offset;
		k_thread_create(&preemptor, preemptor_stack,
				K_THREAD_STACK_SIZEOF(preemptor_stack), preempt,
				NULL, NULL, NULL, K_PRIO_PREEMPT(0), 0, K_NO_WAIT);
#ifdef CONFIG_FPU
		__asm__ volatile("vmov s16, %0" :: "r"(0x3f800000U) : "s16");
#endif
		k_sem_give(&start);
		restore_test_armed = 0U;
		zassert_equal(count, PREEMPTIONS, "restore interrupt did not switch threads");
#ifdef CONFIG_FPU
		uint32_t value;

		__asm__ volatile("vmov %0, s16" : "=r"(value));
		zassert_equal(value, 0x3f800000U, "FP context was not preserved");
#endif
		zassert_ok(k_thread_join(&preemptor, K_FOREVER));
	}

	irq_disable(test_irq);
	victim = NULL;
	k_thread_priority_set(k_current_get(), priority);
}
