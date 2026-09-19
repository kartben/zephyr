/*
 * Copyright (c) 2024 Espressif Systems (Shanghai) Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/interrupt_controller/intc_esp32.h>

#include <soc.h>
#include <esp_log.h>
#include <esp_cpu.h>
#include <esp_rom_serial_output.h>

#include <esp_mcuboot_image.h>
#include <esp_memory_utils.h>
#include <hw_init.h>

#ifdef CONFIG_SMP
#include <ipi.h>
#include <zephyr/zsr.h>
#include <soc/dport_reg.h>
#include <soc/interrupts.h>
#include <xtensa/config/core-isa.h>
#include <xtensa/corebits.h>
#endif

#define TAG "amp"

/* Core-1 hardware start sequence. Shared by the AMP image loader and, under
 * CONFIG_SMP, by arch_cpu_start(). The two are mutually exclusive.
 */
#if defined(CONFIG_SOC_ENABLE_APPCPU) || defined(CONFIG_SMP)
void esp_appcpu_start(void *entry_point)
{
	esp_cpu_unstall(1);

	if (!REG_GET_BIT(SYSTEM_CORE_1_CONTROL_0_REG, SYSTEM_CONTROL_CORE_1_CLKGATE_EN)) {
		REG_SET_BIT(SYSTEM_CORE_1_CONTROL_0_REG, SYSTEM_CONTROL_CORE_1_CLKGATE_EN);
		REG_CLR_BIT(SYSTEM_CORE_1_CONTROL_0_REG, SYSTEM_CONTROL_CORE_1_RUNSTALL);
		REG_SET_BIT(SYSTEM_CORE_1_CONTROL_0_REG, SYSTEM_CONTROL_CORE_1_RESETTING);
		REG_CLR_BIT(SYSTEM_CORE_1_CONTROL_0_REG, SYSTEM_CONTROL_CORE_1_RESETTING);
	}

	esp_rom_ets_set_appcpu_boot_addr((void *)entry_point);

	esp_cpu_reset(1);
}
#endif /* CONFIG_SOC_ENABLE_APPCPU || CONFIG_SMP */

/* SMP support */
#ifdef CONFIG_SMP

struct cpustart_rec {
	int cpu;
	arch_cpustart_t fn;
	char *stack_top;
	void *arg;
	int vecbase;
	volatile int *alive;
};

/* Temporary bring-up tracing: lock-free and usable before the per-CPU
 * pointer exists, unlike smp_log().
 */
/* CCOUNT is a per-CPU register, but the system clock's baseline
 * (timer_core_last_cycle in drivers/timer/system_timer_generic.h) is global.
 * Core 1's CCOUNT starts counting only when it is released from reset, so
 * without this the two cores compute their tick deltas against unrelated
 * counters and the clock runs fast. Both count the same 240 MHz PLL, so a
 * single alignment at startup holds for good: CPU0 republishes its CCOUNT
 * while it waits for the handshake, and core 1 adopts the last value.
 */
static volatile uint32_t ccount_sync;

static ALWAYS_INLINE uint32_t zrd_ccount(void)
{
	uint32_t v;

	__asm__ volatile("rsr.CCOUNT %0" : "=r"(v));
	return v;
}

volatile struct cpustart_rec *start_rec;
static void *appcpu_top;
static bool cpus_active[CONFIG_MP_MAX_NUM_CPUS];
static struct k_spinlock loglock;

/* Note that the logging done here is ACTUALLY REQUIRED FOR RELIABLE
 * OPERATION on the original ESP32 -- see the long comment in
 * soc/espressif/esp32/esp32-mp.c. Whether the S3 ROM needs the same
 * nudge is not established; it is kept because it costs nothing at
 * boot and makes a failed core-1 start visible on the console.
 */
void smp_log(const char *msg)
{
	k_spinlock_key_t key = k_spin_lock(&loglock);

	while (*msg) {
		esp_rom_output_tx_one_char(*msg++);
	}
	esp_rom_output_tx_one_char('\r');
	esp_rom_output_tx_one_char('\n');

	k_spin_unlock(&loglock, key);
}

/* Cross-core interrupt (scheduler IPI).
 *
 * The four FROM_CPU sources are ordinary level-triggered peripheral
 * sources: whichever core routes source N through its own interrupt
 * matrix receives it. We follow the IDF convention, which the esp32
 * port does NOT: core N owns FROM_CPU_INTR<N> and allocates it on
 * itself, a sender writes the *target's* register, and the ISR clears
 * its *own*. Allocating both on core 0 -- as esp32-mp.c does -- routes
 * neither source to core 1 and makes the ISR clear the wrong bit,
 * leaving a level source asserted.
 */
static const uint32_t ipi_reg[2] = {
	SYSTEM_CPU_INTR_FROM_CPU_0_REG,
	SYSTEM_CPU_INTR_FROM_CPU_1_REG,
};

IRAM_ATTR static void esp_crosscore_isr(void *arg)
{
	ARG_UNUSED(arg);

	/* Clear our own source before servicing: it is level-triggered,
	 * so an IPI raised while we are inside z_sched_ipi() must leave
	 * the bit set and re-enter, not be swallowed.
	 */
	WRITE_PERI_REG(ipi_reg[esp_core_id()], 0);

	z_sched_ipi();
}

#define IPI_INTR_FLAGS(node)                                                                       \
	(ESP_PRIO_TO_FLAGS(DT_IRQ_BY_IDX(node, 0, priority)) |                                     \
	 ESP_INT_FLAGS_CHECK(DT_IRQ_BY_IDX(node, 0, flags)) | ESP_INTR_FLAG_IRAM)

/* Runs on the core it registers for. */
static void esp_crosscore_int_init(void)
{
	int ret;

	if (esp_core_id() == 0) {
		ret = esp_intr_alloc(DT_IRQ_BY_IDX(DT_NODELABEL(ipi0), 0, irq),
				     IPI_INTR_FLAGS(DT_NODELABEL(ipi0)), esp_crosscore_isr, NULL,
				     NULL);
	} else {
		ret = esp_intr_alloc(DT_IRQ_BY_IDX(DT_NODELABEL(ipi1), 0, irq),
				     IPI_INTR_FLAGS(DT_NODELABEL(ipi1)), esp_crosscore_isr, NULL,
				     NULL);
	}

	if (ret != 0) {
		smp_log("ESP32S3: failed to allocate crosscore interrupt");
	}
}

void arch_sched_directed_ipi(uint32_t cpu_bitmap)
{
	unsigned int self = esp_core_id();

	for (unsigned int i = 0; i < CONFIG_MP_MAX_NUM_CPUS; i++) {
		if (i == self || (cpu_bitmap & BIT(i)) == 0U) {
			continue;
		}
		WRITE_PERI_REG(ipi_reg[i], BIT(0));
	}
}

void arch_sched_broadcast_ipi(void)
{
	arch_sched_directed_ipi(IPI_ALL_CPUS_MASK);
}

static void core_intr_matrix_clear(void)
{
	uint32_t core_id = esp_cpu_get_core_id();

	for (int i = 0; i < ETS_MAX_INTR_SOURCE; i++) {
		intr_matrix_set(core_id, i, ETS_INVALID_INUM);
	}
}

static void appcpu_entry2(void)
{
	volatile int ps, ie;

	/* First thing, before this core's timer is armed: adopt CPU0's cycle
	 * count so the shared tick baseline means the same thing on both.
	 */
	__asm__ volatile("wsr.CCOUNT %0" : : "r"(ccount_sync));

	/* Copy over VECBASE from the main CPU for an initial value
	 * (will need to revisit this if we ever allow a user API to
	 * change interrupt vectors at runtime). Make sure interrupts
	 * are locally disabled, then synthesize a PS value that will
	 * enable them for the user code to pass to irq_unlock() later.
	 */
	__asm__ volatile("rsr.PS %0" : "=r"(ps));
	ps &= ~(XCHAL_PS_EXCM_MASK | XCHAL_PS_INTLEVEL_MASK);
	__asm__ volatile("wsr.PS %0" : : "r"(ps));

	ie = 0;
	__asm__ volatile("wsr.INTENABLE %0" : : "r"(ie));
	__asm__ volatile("wsr.VECBASE %0" : : "r"(start_rec->vecbase));
	__asm__ volatile("rsync");

	/* Set up the CPU pointer. Really this should be xtensa arch
	 * code, not in the ESP32-S3 layer.
	 */
	_cpu_t *cpu = &_kernel.cpus[1];

	__asm__ volatile("wsr %0, " ZSR_CPU_STR : : "r"(cpu));

	/* The ROM left this core's matrix in whatever state it booted
	 * with; clear it before allocating anything, as IDF does in
	 * call_start_cpu1(). The esp32 port omits this.
	 */
	core_intr_matrix_clear();

	/* So a core-1 reset idles in the ROM rather than re-entering us. */
	esp_rom_ets_set_appcpu_boot_addr((void *)0);

	smp_log("ESP32S3: APPCPU running");

	/* This core's scheduler IPI is registered later, from a kernel
	 * context -- see esp_crosscore_init_all(). esp_intr_alloc() cannot
	 * be called from here.
	 */

	/* start_rec lives on CPU0's stack and dies with the handshake,
	 * so latch what we still need out of it first.
	 */
	arch_cpustart_t fn = start_rec->fn;
	void *arg = start_rec->arg;

	*start_rec->alive = 1;
	fn(arg);
}

/* Defines a locally callable "function" named z_appcpu_stack_switch().
 * The first argument (in register a2 post-ENTRY) is the new stack
 * pointer to go into register a1. The second (a3) is the entry point.
 * Because this never returns, a0 is used as a scratch register then
 * set to zero for the called function (a null return value is the
 * signal for "top of stack" to the debugger).
 */
void z_appcpu_stack_switch(void *stack, void *entry);
__asm__("\n"
	".align 4"			"\n"
	"z_appcpu_stack_switch:"	"\n\t"

	"entry a1, 16"			"\n\t"

	/* Subtle: we want the stack to be 16 bytes higher than the
	 * top on entry to the called function, because the ABI forces
	 * it to assume that those 16 bytes are for its caller's
	 * registers and can be spilled. We have no caller, so the
	 * bytes would be wasted otherwise.
	 */
	"addi a1, a2, 16"		"\n\t"

	/* Clear WINDOWSTART so called functions never try to spill
	 * our callers' registers into the now-changed stack.
	 */
	"movi a0, 0"			"\n\t"
	"wsr.WINDOWSTART a0"		"\n\t"

	/* Clear CALLINC field of PS (you'd think it would, but ENTRY
	 * doesn't actually do that) so the callee doesn't scribble
	 * into our stack frame.
	 */
	"rsr.PS a0"			"\n\t"
	"movi a2, 0xfffcffff"		"\n\t"
	"and a0, a0, a2"		"\n\t"
	"wsr.PS a0"			"\n\t"

	"rsync"				"\n\t"
	"movi a0, 0"			"\n\t"
	"jx a3"				"\n\t");

/* Carefully constructed to use no stack beyond compiler-generated ABI
 * instructions. Stack pointer is pointing to unusable memory as the
 * ROM has no idea we are here, so we must switch before doing
 * anything else.
 */
void IRAM_ATTR appcpu_entry1(void)
{
	z_appcpu_stack_switch(appcpu_top, appcpu_entry2);
}

void arch_cpu_start(int cpu_num, k_thread_stack_t *stack, int sz, arch_cpustart_t fn, void *arg)
{
	volatile struct cpustart_rec sr;
	int vb;
	volatile int alive_flag;

	__ASSERT(cpu_num == 1, "ESP32-S3 has only two cores");

	__asm__ volatile("rsr.VECBASE %0\n\t" : "=r"(vb));

	alive_flag = 0;

	sr.cpu = cpu_num;
	sr.fn = fn;
	sr.stack_top = K_KERNEL_STACK_BUFFER(stack) + sz;
	sr.arg = arg;
	sr.vecbase = vb;
	sr.alive = &alive_flag;

	appcpu_top = K_KERNEL_STACK_BUFFER(stack) + sz;

	start_rec = &sr;

	esp_appcpu_start(appcpu_entry1);

	/* Keep publishing until core 1 signals: it adopts whatever it last saw,
	 * so the residual offset is only the store-to-load delay between cores.
	 */
	while (!alive_flag) {
		ccount_sync = zrd_ccount();
	}

	cpus_active[0] = true;
	cpus_active[cpu_num] = true;

	smp_log("ESP32S3: APPCPU initialized");
}

/* esp_intr_alloc() must run on the core it allocates for, but it is a
 * kernel-context API: intc_esp32.c takes irq_lock(), which under SMP is
 * z_smp_global_lock() and dereferences _current. On a secondary core
 * _current does not exist until smp_init_top() installs the dummy
 * thread, which is after arch_cpu_start() returns -- so the registration
 * cannot happen in the core's entry path.
 *
 * Register at INIT_LEVEL_SMP instead, which runs after z_smp_init(), and
 * reach each secondary core through a thread pinned to it. Until this
 * runs the scheduler just falls back to waking the other core on its
 * next timer tick.
 */
#define ZRD_SECONDARY_CPUS (CONFIG_MP_MAX_NUM_CPUS - 1)

static K_THREAD_STACK_ARRAY_DEFINE(ipi_init_stacks, ZRD_SECONDARY_CPUS, 1024);
static struct k_thread ipi_init_threads[ZRD_SECONDARY_CPUS];

static void ipi_init_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	esp_crosscore_int_init();
}

static int esp_crosscore_init_all(void)
{
	/* Runs on CPU0, so CPU0 registers itself directly. */
	esp_crosscore_int_init();

	/* Deliberately fire-and-forget. Waiting here would deadlock: this runs
	 * before main(), and the only thing that would promptly tell a
	 * secondary core to pick the thread up is the IPI being installed. Each
	 * core registers whenever it next schedules; until then the scheduler
	 * falls back to that core's timer tick, which is correct, just slower.
	 */
	for (int cpu = 1; cpu < arch_num_cpus(); cpu++) {
		k_tid_t tid = k_thread_create(&ipi_init_threads[cpu - 1], ipi_init_stacks[cpu - 1],
					      K_THREAD_STACK_SIZEOF(ipi_init_stacks[cpu - 1]),
					      ipi_init_fn, NULL, NULL, NULL, 0, 0, K_FOREVER);

		k_thread_name_set(tid, "ipi-init");
		if (k_thread_cpu_pin(tid, cpu) == 0) {
			k_thread_start(tid);
		}
	}

	return 0;
}
SYS_INIT(esp_crosscore_init_all, SMP, 0);

bool arch_cpu_active(int cpu_num)
{
	return cpus_active[cpu_num];
}

#endif /* CONFIG_SMP */

/* AMP support */
#ifdef CONFIG_SOC_ENABLE_APPCPU

#include <bootloader_flash_priv.h>

#define sys_mmap   bootloader_mmap
#define sys_munmap bootloader_munmap

static int load_segment(uint32_t src_addr, uint32_t src_len, uint32_t dst_addr)
{
	const uint32_t *data = (const uint32_t *)sys_mmap(src_addr, src_len);

	if (!data) {
		ESP_EARLY_LOGE(TAG, "%s: mmap failed", __func__);
		return -1;
	}

	volatile uint32_t *dst = (volatile uint32_t *)dst_addr;

	for (int i = 0; i < src_len / 4; i++) {
		dst[i] = data[i];
	}

	sys_munmap(data);

	return 0;
}

int IRAM_ATTR esp_appcpu_image_load(unsigned int hdr_offset, unsigned int *entry_addr)
{
	const uint32_t fa_offset = PARTITION_OFFSET(slot0_appcpu_partition);
	const uint32_t fa_size = PARTITION_SIZE(slot0_appcpu_partition);
	const uint8_t fa_id = PARTITION_ID(slot0_appcpu_partition);

	if (entry_addr == NULL) {
		ESP_EARLY_LOGE(TAG, "Can't return the entry address. Aborting!");
		abort();
		return -1;
	}

	uint32_t mcuboot_header[8] = {0};
	esp_image_load_header_t image_header = {0};

	const uint32_t *data = (const uint32_t *)sys_mmap(fa_offset, 0x80);

	memcpy((void *)&mcuboot_header, data, sizeof(mcuboot_header));
	memcpy((void *)&image_header, data + (hdr_offset / sizeof(uint32_t)),
	       sizeof(esp_image_load_header_t));

	sys_munmap(data);

	if (image_header.header_magic == ESP_LOAD_HEADER_MAGIC) {
		ESP_EARLY_LOGI(TAG,
			"APPCPU image, area id: %d, offset: 0x%x, hdr.off: 0x%x, size: %d kB",
			fa_id, fa_offset, hdr_offset, fa_size / 1024);
	} else if ((image_header.header_magic & 0xff) == 0xE9) {
		ESP_EARLY_LOGE(TAG, "ESP image format is not supported");
		abort();
	} else {
		ESP_EARLY_LOGE(TAG, "Unknown or empty image detected. Aborting!");
		abort();
	}

	if (!esp_ptr_in_iram((void *)image_header.iram_dest_addr) ||
	    !esp_ptr_in_iram((void *)(image_header.iram_dest_addr + image_header.iram_size))) {
		ESP_EARLY_LOGE(TAG, "IRAM region in load header is not valid. Aborting");
		abort();
	}

	if (!esp_ptr_in_dram((void *)image_header.dram_dest_addr) ||
	    !esp_ptr_in_dram((void *)(image_header.dram_dest_addr + image_header.dram_size))) {
		ESP_EARLY_LOGE(TAG, "DRAM region in load header is not valid. Aborting");
		abort();
	}

	if (!esp_ptr_in_iram((void *)image_header.entry_addr)) {
		ESP_EARLY_LOGE(TAG, "Application entry point (%xh) is not in IRAM. Aborting",
			   image_header.entry_addr);
		abort();
	}

	ESP_EARLY_LOGI(TAG, "IRAM segment: paddr=%08xh, vaddr=%08xh, size=%05xh (%6d) load",
		   (fa_offset + image_header.iram_flash_offset), image_header.iram_dest_addr,
		   image_header.iram_size, image_header.iram_size);

	load_segment(fa_offset + image_header.iram_flash_offset, image_header.iram_size,
		     image_header.iram_dest_addr);

	ESP_EARLY_LOGI(TAG, "DRAM segment: paddr=%08xh, vaddr=%08xh, size=%05xh (%6d) load",
		   (fa_offset + image_header.dram_flash_offset), image_header.dram_dest_addr,
		   image_header.dram_size, image_header.dram_size);

	load_segment(fa_offset + image_header.dram_flash_offset, image_header.dram_size,
		     image_header.dram_dest_addr);

	ESP_EARLY_LOGI(TAG, "IROM segment: paddr=%08xh, vaddr=%08xh, size=%05xh (%6d) map",
		   (fa_offset + image_header.irom_flash_offset), image_header.irom_map_addr,
		   image_header.irom_size, image_header.irom_size);

	ESP_EARLY_LOGI(TAG, "DROM segment: paddr=%08xh, vaddr=%08xh, size=%05xh (%6d) map",
		   (fa_offset + image_header.drom_flash_offset), image_header.drom_map_addr,
		   image_header.drom_size, image_header.drom_size);

	struct rom_segments rom = {
		image_header.drom_map_addr,
		image_header.drom_flash_offset + fa_offset,
		image_header.drom_size,
		image_header.irom_map_addr,
		image_header.irom_flash_offset + fa_offset,
		image_header.irom_size,
	};

	map_rom_segments(1, &rom);

	ESP_EARLY_LOGI(TAG, "Application start=%xh\n\n", image_header.entry_addr);
	esp_rom_output_tx_wait_idle(0);

	assert(entry_addr != NULL);
	*entry_addr = image_header.entry_addr;

	return 0;
}

void esp_appcpu_image_stop(void)
{
	esp_cpu_stall(1);
}

void esp_appcpu_image_start(unsigned int hdr_offset)
{
	static int started;
	unsigned int entry_addr = 0;

	if (started) {
		printk("APPCPU already started.\r\n");
		return;
	}

	/* Input image meta header, output appcpu entry point */
	esp_appcpu_image_load(hdr_offset, &entry_addr);

	esp_appcpu_start((void *)entry_addr);
}

int esp_appcpu_init(void)
{
	/* Load APPCPU image using image header offset
	 * (skipping the MCUBoot header)
	 */
	esp_appcpu_image_start(0x20);

	return 0;
}

#if !defined(CONFIG_MCUBOOT)
extern int esp_appcpu_init(void);
SYS_INIT(esp_appcpu_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
#endif

#endif /* CONFIG_SOC_ENABLE_APPCPU */
