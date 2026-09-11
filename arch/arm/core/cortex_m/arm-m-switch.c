/*
 * Copyright 2025 The ChromiumOS Authors
 * Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/sys/util.h>
#include <ksched.h>

/* The basic exception frame, popped by the hardware during return */
struct hw_frame_base {
	uint32_t r0, r1, r2, r3;
	uint32_t r12;
	uint32_t lr;
	uint32_t pc;
	uint32_t apsr;
};

/* The hardware frame pushed when entry is taken with FPU active */
struct hw_frame_fpu {
	struct hw_frame_base base;
	uint32_t s_regs[16];
	uint32_t fpscr;
	uint32_t reserved;
};

/* The hardware frame pushed when entry happens with a misaligned stack */
struct hw_frame_align {
	struct hw_frame_base base;
	uint32_t align_pad;
};

/* Both of the above */
struct hw_frame_align_fpu {
	struct hw_frame_fpu base;
	uint32_t align_pad;
};

/* Zephyr's frame for a suspended thread.  The top of it is laid out
 * exactly as a hardware exception frame, so a thread suspended by an
 * interrupt needs no conversion: the frame the CPU pushed is already in
 * place and only the callee-saved block below it has to be filled in.
 * Resuming such a thread on interrupt exit is likewise just a matter of
 * pointing PSP at .base.
 */
struct switch_frame {
#ifdef CONFIG_BUILTIN_STACK_GUARD
	uint32_t psplim;
#endif
	uint32_t r4, r5, r6, r7, r8, r9, r10, r11;
	struct hw_frame_base base;
};

/* The callee-saved block the exception exit fixup assembly moves with a
 * single ldm/stm, now that it is contiguous in both directions.
 */
#define SWITCH_CS(f) (&(f)->r4)

union u_frame {
	struct switch_frame sw;
};

/* Switch frame with the have_fpu flag prepended (zero value here) */
struct z_frame {
#ifdef CONFIG_FPU
	uint32_t have_fpu;
#endif
	union u_frame u;
};

/* Switch frame for a thread holding live FPU context.  The hardware's FP
 * sub-frame is left exactly where the CPU pushed it, above the integer
 * frame, so that resuming through an FP exception return restores s0-s15
 * and FPSCR at no cost.  Only the callee-saved half, which the hardware
 * never touches, has to be spilled below.
 */
struct z_frame_fpu {
	uint32_t have_fpu;
	uint32_t s_regs[16]; /* s16-s31 */
	union u_frame u;
	uint32_t hw_s_regs[16]; /* s0-s15, hardware layout */
	uint32_t fpscr;
	uint32_t reserved;
};

BUILD_ASSERT(offsetof(struct z_frame_fpu, hw_s_regs) - offsetof(struct z_frame_fpu, u) ==
		     sizeof(struct switch_frame),
	     "FP sub-frame must sit directly above the integer frame");
BUILD_ASSERT(sizeof(struct switch_frame) == ARM_M_SW_FRAME_SZ,
	     "ARM_M_SW_FRAME_SZ out of sync with struct switch_frame");
BUILD_ASSERT(sizeof(struct z_frame_fpu) - offsetof(struct z_frame_fpu, hw_s_regs) ==
		     ARM_M_FP_ABOVE_SZ,
	     "ARM_M_FP_ABOVE_SZ out of sync with struct z_frame_fpu");

#ifdef CONFIG_FPU
/* Extra stack the switch layer needs below what the CPU already pushed */
uint32_t arm_m_switch_stack_buffer =
	sizeof(struct z_frame_fpu) - sizeof(struct hw_frame_fpu) - sizeof(struct hw_frame_base);
#else
uint32_t arm_m_switch_stack_buffer = sizeof(struct z_frame) - sizeof(struct hw_frame_base);
#endif

struct arm_m_cs_ptrs arm_m_cs_ptrs;

#ifdef CONFIG_LTO
/* Toolchain workaround: when building with LTO, gcc seems unable to
 * notice the external references in the assembly for arm_m_exc_exit
 * below, and drops the symbols before the final link.  Use this
 * global to store pointers in arm_m_new_stack(), wasting a few bytes
 * of code & data.
 */
void *arm_m_lto_refs[2];
#endif

/* Bitmask to determine if the XPSR indicates the exception frame was padded */
#define XPSR_STACK_ALIGN BIT(9)

/* Unit test hook, unused in production */
void *arm_m_last_switch_handle;

/* Global holder for the location of the saved LR in the entry frame. */
uint32_t *arm_m_exc_lr_ptr;

/* Dummy used in arch_switch() when USERSPACE=y */
uint32_t arm_m_switch_control;

/* Removes the hardware's 4 byte stack alignment padding, if present, by
 * shifting the exception frame up over it and clearing the flag.  That
 * keeps every suspended frame a uniform size, so the cooperative restore
 * needs no runtime test for it.  Returns the new frame address.
 */
static struct hw_frame_base *unpad_frame(struct hw_frame_base *hw, bool fpu)
{
	uint32_t *w = (uint32_t *)hw;
	int words = (IS_ENABLED(CONFIG_FPU) && fpu)
			    ? (int)(sizeof(struct hw_frame_fpu) / sizeof(uint32_t))
			    : (int)(sizeof(struct hw_frame_base) / sizeof(uint32_t));

	if ((hw->apsr & XPSR_STACK_ALIGN) == 0U) {
		return hw;
	}

	for (int i = words - 1; i >= 0; i--) {
		w[i + 1] = w[i];
	}

	hw = (struct hw_frame_base *)&w[1];
	hw->apsr &= ~XPSR_STACK_ALIGN;
	return hw;
}

/* The arch/cpu/toolchain are horrifyingly inconsistent with how the
 * thumb bit is treated in runtime addresses.  The PC target for a B
 * instruction must have it set.  The PC pushed from an exception has
 * is unset.  The linker puts functions at even addresses, obviously,
 * but the symbol address exposed at runtime has it set.  Exception
 * return ignores it.  Use this to avoid insanity.
 */
static bool pc_match(uint32_t pc, void *addr)
{
	return ((pc ^ (uint32_t) addr) & ~1) == 0;
}

/* Reports if the passed return address is a valid EXC_RETURN (high
 * four bits set) that will restore to the PSP running in thread mode
 * (low four bits == 0xd).  That is an interrupted Zephyr thread
 * context.  For everything else, we just return directly via the
 * hardware-pushed stack frame with no special handling. See ARMv7M
 * manual B1.5.8.
 */
static bool is_thread_return(uint32_t lr)
{
	return (lr & 0xf000000f) == 0xf000000d;
}

/* Returns true if the EXC_RETURN address indicates a FPU subframe was
 * pushed to the stack.  See ARMv7M manual B1.5.8.
 */
static bool fpu_state_pushed(uint32_t lr)
{
	return IS_ENABLED(CONFIG_CPU_HAS_FPU) ? !(lr & 0x10) : false;
}

/* ICI/IT instruction fault workarounds
 *
 * ARM Cortex M has what amounts to a design bug.  The architecture
 * inherits several unpipelined/microcoded "ICI/IT" instruction forms
 * that take many cycles to complete (LDM/STM and the Thumb "IT"
 * conditional frame are the big ones).  But out of a desire to
 * minimize interrupt latency, the CPU is allowed to halt and resume
 * these instructions mid-flight while they are partially completed.
 * The relevant bits of state are stored in the EPSR fields of the
 * xPSR register (see ARMv7-M manual B1.4.2).  But (and this is the
 * design bug) those bits CANNOT BE WRITTEN BY SOFTWARE.  They can
 * only be modified by exception return.
 *
 * This means that if a Zephyr thread takes an interrupt
 * mid-ICI/IT-instruction, then switches to another thread on exit,
 * and then that thread is resumed by a cooperative switch and not an
 * interrupt, the instruction will lose the state and restart from
 * scratch.  For LDM/STM that's generally idempotent for memory (but
 * not MMIO!), but for IT that means that the restart will re-execute
 * arbitrary instructions that may not be idempotent (e.g. "addeq r0,
 * r0, #1" can't be done twice, because you would add two to r0!)
 *
 * The fix is to check for this condition (which is very rare) on
 * interrupt exit when we are switching, and if we discover we've
 * interrupted such an instruction we swap the return address with a
 * trampoline that uses a UDF instruction to immediately trap to the
 * undefined instruction handler, which then recognizes the fixup
 * address as special and immediately returns back into the thread
 * with the correct EPSR value and resume PC (which have been stashed
 * in the thread struct).  The overhead for the normal case is just a
 * few cycles for the test.
 */
__attribute__((naked)) void arm_m_iciit_stub(void)
{
	__asm__("udf #0;");
}

/* Called out of interrupt entry to test for an interrupted instruction */
static void iciit_fixup(struct k_thread *th, struct hw_frame_base *hw, uint32_t xpsr)
{
#ifdef CONFIG_MULTITHREADING
	if ((xpsr & 0x0600fc00) != 0) {
		/* Stash original return address, replace with hook */
		th->arch.iciit_pc = hw->pc;
		th->arch.iciit_apsr = hw->apsr;
		hw->pc = (uint32_t)arm_m_iciit_stub;
	}
#endif
}

/* Called out of fault handler from the UDF after an arch_switch() */
bool arm_m_iciit_check(uint32_t msp, uint32_t psp, uint32_t lr)
{
	struct hw_frame_base *f = (void *)psp;

	/* Look for undefined instruction faults from our stub */
	if (pc_match(f->pc, arm_m_iciit_stub)) {
		if (is_thread_return(lr)) {
			f->pc = _current->arch.iciit_pc;
			f->apsr = _current->arch.iciit_apsr;
			_current->arch.iciit_pc = 0;
			return true;
		}
	}
	return false;
}



/* Converts, in place, a pickled "switch" frame from a suspended
 * thread to a "synthesized" format that can be restored by the CPU
 * hardware on exception exit.
 */
static void *arm_m_switch_to_cpu(void *sp)
{
	struct switch_frame *sw;

#ifdef CONFIG_FPU
	if (*(uint32_t *)sp != 0U) {
		struct z_frame_fpu *zf = CONTAINER_OF(sp, struct z_frame_fpu, have_fpu);

		/* The hardware restores s0-s15 and FPSCR from the frame we
		 * are returning through, so only the callee-saved half has
		 * to be reloaded here.
		 */
		__asm__ volatile("vldm %0, {s16-s31}" ::"r"(&zf->s_regs[0]));
		arm_m_cs_ptrs.exc_lr = (void *)EXC_RETURN_FPU;
		sw = &zf->u.sw;
	} else {
		struct z_frame *z = CONTAINER_OF(sp, struct z_frame, have_fpu);

		arm_m_cs_ptrs.exc_lr = (void *)EXC_RETURN_INT;
		sw = &z->u.sw;
	}
#else
	sw = sp;
#endif

	IF_ENABLED(CONFIG_BUILTIN_STACK_GUARD,
		   (__asm__ volatile("msr psplim, %0" ::"r"(sw->psplim));))

	/* Mark the callee-saved pointer for the fixup assembly */
	arm_m_cs_ptrs.in = SWITCH_CS(sw);

	return &sw->base;
}

static void *arm_m_cpu_to_switch(struct k_thread *th, void *sp, bool fpu)
{
	struct hw_frame_base *base = sp;
	struct switch_frame *sw;

	if (IS_ENABLED(CONFIG_FPU) && fpu) {
		uint32_t dummy = 0;

		/* Lazy FPU stacking is enabled, so before we touch the
		 * stack frame we have to tickle the FPU to force it to
		 * spill the caller-save registers.  Then clear
		 * CONTROL.FPCA which gets set again by that instruction.
		 */
		__asm__ volatile("vmov %0, s0;"
				 "mrs %0, control;"
				 "bic %0, %0, #4;"
				 "msr control, %0;"
				 : "+r"(dummy));
	}

	/* Detects interrupted ICI/IT instructions and rigs up thread
	 * to trap the next time it runs
	 */
	iciit_fixup(th, base, base->apsr);

	/* The hardware frame already is the top of a switch frame.  All
	 * that remains is to normalize away the alignment padding and to
	 * set the thumb bit, which the software restore path branches
	 * through (the hardware ignores it on exception return).
	 */
	base = unpad_frame(base, fpu);
	base->pc |= 1;

	sw = CONTAINER_OF(base, struct switch_frame, base);

	IF_ENABLED(CONFIG_BUILTIN_STACK_GUARD,
		   (__asm__ volatile("mrs %0, psplim" : "=r"(sw->psplim));))

	/* Mark the callee-saved pointer for the fixup assembly */
	arm_m_cs_ptrs.out = SWITCH_CS(sw);

#ifdef CONFIG_FPU
	if (fpu) {
		struct z_frame_fpu *zf =
			CONTAINER_OF(CONTAINER_OF(sw, union u_frame, sw), struct z_frame_fpu, u);

		__asm__ volatile("vstm %0, {s16-s31}" ::"r"(&zf->s_regs[0]) : "memory");
		zf->have_fpu = 1;
		return &zf->have_fpu;
	}

	struct z_frame *z = CONTAINER_OF(CONTAINER_OF(sw, union u_frame, sw), struct z_frame, u);

	z->have_fpu = 0;
	return &z->have_fpu;
#else
	return sw;
#endif
}

void *arm_m_new_stack(char *base, uint32_t sz, void *entry, void *arg0, void *arg1, void *arg2,
		      void *arg3)
{
	struct switch_frame *sw;
	uint32_t baddr;

#ifdef CONFIG_LTO
	arm_m_lto_refs[0] = &arm_m_cs_ptrs;
	arm_m_lto_refs[1] = arm_m_must_switch;
#endif

#ifdef CONFIG_MULTITHREADING
	/* Kludgey global initialization, stash computed pointers to
	 * the LR frame location and fixup address into these
	 * variables for use by arm_m_exc_tail().  Should move to arch
	 * init somewhere once arch_switch is better integrated
	 */
	char *stack = (char *)K_KERNEL_STACK_BUFFER(z_interrupt_stacks[0]);
	uint32_t *s_top = (uint32_t *)(stack + K_KERNEL_STACK_SIZEOF(z_interrupt_stacks[0]));

	arm_m_exc_lr_ptr = &s_top[-1];
	arm_m_cs_ptrs.lr_fixup = (void *)(1 | (uint32_t)arm_m_exc_exit); /* thumb bit! */
#endif

	baddr = ((uint32_t)base + 7) & ~7;

	sz = ((uint32_t)(base + sz) - baddr) & ~7;

	if (sz < sizeof(struct switch_frame)) {
		return NULL;
	}

	/* Note: a useful trick here would be to initialize LR to
	 * point to cleanup code, avoiding the need for the
	 * z_thread_entry wrapper, saving a few words of stack frame
	 * and a few cycles on thread entry.
	 */
	sw = (void *)(baddr + sz - sizeof(*sw));

	/* Written field by field on purpose: assigning a compound literal
	 * makes the compiler emit a memset() call for the whole frame, which
	 * costs ~200 instructions with a byte-at-a-time libc implementation.
	 */
	IF_ENABLED(CONFIG_BUILTIN_STACK_GUARD, (sw->psplim = baddr;))
	sw->base.apsr = 0x1000000; /* thumb bit here too! */
	sw->base.r0 = (uint32_t)arg0;
	sw->base.r1 = (uint32_t)arg1;
	sw->base.r2 = (uint32_t)arg2;
	sw->base.r3 = (uint32_t)arg3;
	sw->r4 = 0;
	sw->r5 = 0;
	sw->r6 = 0;
	sw->r7 = 0;
	sw->r8 = 0;
	sw->r9 = 0;
	sw->r10 = 0;
	sw->r11 = 0;
	sw->base.r12 = 0;
	sw->base.lr = 0;
	sw->base.pc = ((uint32_t)entry) | 1; /* set thumb bit! */

#ifdef CONFIG_FPU
	struct z_frame *zf = CONTAINER_OF(sw, struct z_frame, u.sw);

	zf->have_fpu = false;
	return zf;
#else
	return sw;
#endif
}

bool arm_m_do_switch(struct k_thread *last_thread, void *next);

bool arm_m_must_switch(void)
{
	/* This lock is held until the end of the context switch, at
	 * which point it will be dropped unconditionally. Save a few
	 * cycles by skipping the needless bits of arch_irq_lock().
	 */
	uint32_t pri = _EXC_IRQ_DEFAULT_PRIO;

	__asm__ volatile("msr basepri, %0" ::"r"(pri));

	/* Secure mode transitions can push a non-thread frame to the
	 * stack.  If not enabled, we already know by construction
	 * that we're handling the bottom level of the interrupt stack
	 * and returning to thread mode.
	 */
	if ((IS_ENABLED(CONFIG_ARM_SECURE_FIRMWARE) || IS_ENABLED(CONFIG_ARM_NONSECURE_FIRMWARE)) &&
	    !is_thread_return((uint32_t)arm_m_cs_ptrs.lr_save)) {
		return false;
	}

	struct k_thread *last_thread = _current;
	void *next = z_sched_next_handle(last_thread);

	if (next == NULL) {
		return false;
	}

	arm_m_do_switch(last_thread, next);
	return true;
}

bool arm_m_do_switch(struct k_thread *last_thread, void *next)
{
	void *last;
	bool fpu = fpu_state_pushed((uint32_t)arm_m_cs_ptrs.lr_save);

	__asm__ volatile("mrs %0, psp" : "=r"(last));

#ifdef CONFIG_USERSPACE
	/* Update CONTROL register's nPRIV bit to reflect user/syscall
	 * thread state of the incoming thread.
	 */
	extern char z_syscall_exit_race1, z_syscall_exit_race2;
	struct hw_frame_base *f = last;
	uint32_t control;

	/* Note that the privilege state is stored in the CPU *AND* in
	 * the "mode" field of the thread struct.  This creates an
	 * unavoidable race because the syscall exit code can't
	 * atomically release the lock and set CONTROL.  We detect the
	 * case where a thread is interrupted at exactly that moment
	 * and manually skip over the MSR instruction (which would
	 * otherwise fault on resume because "mode" says that the
	 * thread is unprivileged!).  A future rework should just
	 * pickle the CPU state found in the exception to the frame
	 * and not mess with thread state from within the syscall.
	 */
	if (pc_match(f->pc, &z_syscall_exit_race1)) {
		f->pc = (uint32_t) &z_syscall_exit_race2;
	}

	__asm__ volatile("mrs %0, control" : "=r"(control));
	control = (control & ~1) | (_current->arch.mode & 1);
	__asm__ volatile("msr control, %0" ::"r"(control));
#endif

	last = arm_m_cpu_to_switch(last_thread, last, fpu);
	next = arm_m_switch_to_cpu(next);
	__asm__ volatile("msr psp, %0" ::"r"(next));

	/* Undo a UDF fixup applied at interrupt time, no need: we're
	 * restoring EPSR via interrupt.
	 */
	if (_current->arch.iciit_pc) {
		struct hw_frame_base *n = next;

		n->pc = _current->arch.iciit_pc;
		n->apsr = _current->arch.iciit_apsr;
		_current->arch.iciit_pc = 0;
	}

#if !defined(CONFIG_MULTITHREADING)
	arm_m_last_switch_handle = last;
#elif defined(CONFIG_USE_SWITCH)
	last_thread->switch_handle = last;
#endif

#if defined(CONFIG_USERSPACE) || defined(CONFIG_MPU_STACK_GUARD)
	z_arm_configure_dynamic_mpu_regions(_current);
#endif

#ifdef CONFIG_THREAD_LOCAL_STORAGE
	z_arm_tls_ptr = _current->tls;
#endif
	return true;
}

/* This is handled an inline now for C code, but there are a few spots
 * that need to get to it from assembly (but which IMHO should really
 * be ported to C)
 */
void arm_m_legacy_exit(void)
{
	arm_m_exc_tail();
}

/* We arrive here on return to thread code from exception handlers.
 * We know that r4-r11 of the interrupted thread have been restored
 * (other registers will be forgotten and can be clobbered).  First
 * call arm_m_must_switch() (which handles the other context switch
 * duties), and spill/fill if necessary.  If no context switch is
 * needed, we just return via the original LR.  If we are switching,
 * we synthesize a integer-only EXC_RETURN as FPU state switching was
 * handled in software already.
 */
#ifdef CONFIG_MULTITHREADING
__attribute__((naked)) void arm_m_exc_exit(void)
{
	__asm__("  bl arm_m_must_switch;"
		"  ldr r2, =arm_m_cs_ptrs;"
		"  mov r3, #0;"
		"  ldr lr, [r2, #8];" /* lr_save */
		"  cbz r0, 1f;"
#ifdef CONFIG_FPU
		/* The incoming frame decides whether the hardware has to
		 * pop an FP sub-frame on the way out.
		 */
		"  ldr lr, [r2, #16];" /* exc_lr */
#else
		"  mov lr, #0xfffffffd;" /* integer-only LR */
#endif
		"  ldm r2, {r0, r1};"    /* fields: out, in */
		"  stm r0, {r4-r11};"    /* both are switch frames now, so the */
		"  ldm r1, {r4-r11};"    /* callee-saved block is contiguous */
		"1:\n"
		"  msr basepri, r3;" /* release lock taken in must_switch */
		"  bx lr;");
}
#endif
