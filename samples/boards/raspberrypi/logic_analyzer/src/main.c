/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * GPIO capture on RP2040 and RP2350.
 *
 * Free-run is one PIO instruction per sample (IN PINS, 32), so the sample
 * rate equals the state-machine clock, which is clk_sys divided by the PIO
 * clock divider. Edge trigger samples in a two-instruction loop, so that
 * rate is half. DMA copies the RX FIFO into SRAM; the CPU only arms the
 * transfer and reads it back after the state machine stops.
 *
 * The programs are built at runtime with the Pico SDK encoders.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/dt-bindings/clock/rpi_pico_clock_common.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/barrier.h>

#define LA_PIN_BASE	 2U
#define LA_PIN_COUNT	 21U
#define LA_TRIGGER_PIN	 22U
#define LA_TRIGGER_BIT	 (LA_TRIGGER_PIN - LA_PIN_BASE)
#define LA_MAX_SAMPLES	 8192U
#define LA_RING_BYTES	 (LA_MAX_SAMPLES * sizeof(uint32_t))
#define LA_RING_BITS	 15U
#define LA_EDGE_TIMEOUT_MS 30000U
#define LA_DUMP_MAX	 64U
/*
 * The Zephyr DMA driver is left disabled, so it does not own a channel.
 * These two are used directly. The SDK claim helper is not linked unless
 * that driver is enabled.
 */
#define LA_DMA_A	 0U
#define LA_DMA_B	 1U

BUILD_ASSERT((LA_MAX_SAMPLES * sizeof(uint32_t)) == (1U << LA_RING_BITS),
	     "capture buffer must match the DMA ring size");

static const struct device *const la_pio_dev = DEVICE_DT_GET(DT_NODELABEL(pio0));
static const struct device *const la_clk_dev = DEVICE_DT_GET(DT_NODELABEL(clocks));
static const struct reset_dt_spec la_dma_reset = RESET_DT_SPEC_GET(DT_NODELABEL(dma));

/*
 * hardware/pio.h defines pio0 as a macro, and the PIO driver header includes
 * it. Include both only after the devicetree macros above have used pio0 as
 * a node label.
 */
#include <zephyr/drivers/misc/pio_rpi_pico/pio_rpi_pico.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>

static uint32_t sample_buf[LA_MAX_SAMPLES] __aligned(LA_RING_BYTES);

static PIO la_pio;
static uint32_t la_sm;
static uint32_t la_sysclk;
static bool la_ready;
static K_MUTEX_DEFINE(la_lock);

static uint16_t la_free_instr[1];
static uint16_t la_high_instr[8];
static uint16_t la_low_instr[8];
static struct pio_program la_free_prog;
static struct pio_program la_high_prog;
static struct pio_program la_low_prog;
static uint32_t la_off_free;
static uint32_t la_off_high;
static uint32_t la_off_low;

struct la_result {
	bool valid;
	bool triggered;
	uint32_t requested_hz;
	uint32_t actual_hz;
	uint16_t div_int;
	uint8_t div_frac;
	uint32_t cycles;
	uint32_t count;
	uint32_t start;
	uint32_t trigger_index;
	uint32_t post;
};

static struct la_result la_last;

#define LA_OUT(_sh, ...)				\
	do {						\
		if ((_sh) != NULL) {			\
			shell_print((_sh), __VA_ARGS__);\
		} else {				\
			printk(__VA_ARGS__);		\
			printk("\n");			\
		}					\
	} while (false)

static uint32_t la_at(uint32_t logical)
{
	return sample_buf[(la_last.start + logical) % LA_MAX_SAMPLES];
}

static uint32_t la_bit0_edges(void)
{
	uint32_t edges = 0U;
	uint32_t previous;
	uint32_t i;

	if (la_last.count < 2U) {
		return 0U;
	}

	previous = la_at(0) & 1U;
	for (i = 1U; i < la_last.count; i++) {
		uint32_t bit = la_at(i) & 1U;

		if (bit != previous) {
			edges++;
			previous = bit;
		}
	}

	return edges;
}

static void la_print_banner(const struct shell *sh)
{
	LA_OUT(sh, "logic analyzer ready");
	LA_OUT(sh, "sysclk %u Hz", la_sysclk);
	LA_OUT(sh, "free-run max %u sps, 1 cycle/sample", la_sysclk);
	LA_OUT(sh, "edge max %u sps, 2 cycles/sample", la_sysclk / 2U);
	LA_OUT(sh, "channels GPIO%u-GPIO%u, trigger GPIO%u is bit %u",
	       LA_PIN_BASE, LA_PIN_BASE + LA_PIN_COUNT - 1U, LA_TRIGGER_PIN,
	       LA_TRIGGER_BIT);
	LA_OUT(sh, "buffer %u samples", LA_MAX_SAMPLES);
}

static void la_print_result(const struct shell *sh)
{
	uint32_t shown;
	uint32_t i;

	if (!la_last.valid) {
		LA_OUT(sh, "no capture");
		return;
	}

	LA_OUT(sh, "captured %u samples at %u Hz (requested %u Hz)",
	       la_last.count, la_last.actual_hz, la_last.requested_hz);
	LA_OUT(sh, "clkdiv %u + %u/256, %u PIO cycles/sample",
	       la_last.div_int, la_last.div_frac, la_last.cycles);
	if (la_last.triggered) {
		LA_OUT(sh, "trigger at logical sample %u, post %u",
		       la_last.trigger_index, la_last.post);
	}
	LA_OUT(sh, "GPIO%u edges %u", LA_PIN_BASE, la_bit0_edges());

	shown = MIN(la_last.count, 8U);
	for (i = 0U; i < shown; i++) {
		LA_OUT(sh, "sample %u 0x%08x", i, la_at(i));
	}
}

static int la_parse_u32(const char *text, uint32_t *value)
{
	char *end = NULL;
	unsigned long parsed;

	if ((text == NULL) || (text[0] == '\0')) {
		return -EINVAL;
	}

	errno = 0;
	parsed = strtoul(text, &end, 10);
	if ((end == text) || (*end != '\0') || (errno != 0) || (parsed > UINT32_MAX)) {
		return -EINVAL;
	}

	*value = (uint32_t)parsed;
	return 0;
}

static int la_divider(uint32_t cycles, uint32_t requested, uint16_t *div_int,
		      uint8_t *div_frac, uint32_t *actual)
{
	uint64_t step;
	uint64_t q8;
	uint32_t rate = requested;

	if ((requested == 0U) || (cycles == 0U) || (la_sysclk == 0U)) {
		return -EINVAL;
	}

	if (rate > (la_sysclk / cycles)) {
		rate = la_sysclk / cycles;
	}

	step = (uint64_t)rate * cycles;
	q8 = (((uint64_t)la_sysclk << 8) + (step / 2U)) / step;
	if (q8 < 256U) {
		q8 = 256U;
	}
	if (q8 > ((uint64_t)65535U << 8)) {
		q8 = (uint64_t)65535U << 8;
	}

	*div_int = (uint16_t)(q8 >> 8);
	*div_frac = (uint8_t)(q8 & 0xffU);
	*actual = (uint32_t)(((uint64_t)la_sysclk << 8) / ((uint64_t)cycles * q8));
	if (*actual == 0U) {
		return -EINVAL;
	}

	return 0;
}

static void la_build_programs(void)
{
	la_free_instr[0] = (uint16_t)pio_encode_in(pio_pins, 32);
	la_free_prog.instructions = la_free_instr;
	la_free_prog.length = (uint8_t)ARRAY_SIZE(la_free_instr);
	la_free_prog.origin = -1;

	/* Wait until the trigger pin is high, then take `post` samples. */
	la_high_instr[0] = (uint16_t)pio_encode_pull(false, true);
	la_high_instr[1] = (uint16_t)pio_encode_out(pio_x, 32);
	la_high_instr[2] = (uint16_t)pio_encode_in(pio_pins, 32);
	la_high_instr[3] = (uint16_t)pio_encode_jmp_pin(4);
	la_high_instr[4] = (uint16_t)pio_encode_in(pio_pins, 32);
	la_high_instr[5] = (uint16_t)pio_encode_jmp_x_dec(4);
	la_high_instr[6] = (uint16_t)pio_encode_irq_set(false, 0);
	la_high_instr[7] = (uint16_t)pio_encode_jmp(7);
	la_high_prog.instructions = la_high_instr;
	la_high_prog.length = (uint8_t)ARRAY_SIZE(la_high_instr);
	la_high_prog.origin = -1;

	/* Wait until the trigger pin is low, then take `post` samples. */
	la_low_instr[0] = (uint16_t)pio_encode_pull(false, true);
	la_low_instr[1] = (uint16_t)pio_encode_out(pio_x, 32);
	la_low_instr[2] = (uint16_t)pio_encode_in(pio_pins, 32);
	la_low_instr[3] = (uint16_t)pio_encode_jmp_pin(2);
	la_low_instr[4] = (uint16_t)pio_encode_in(pio_pins, 32);
	la_low_instr[5] = (uint16_t)pio_encode_jmp_x_dec(4);
	la_low_instr[6] = (uint16_t)pio_encode_irq_set(false, 0);
	la_low_instr[7] = (uint16_t)pio_encode_jmp(7);
	la_low_prog.instructions = la_low_instr;
	la_low_prog.length = (uint8_t)ARRAY_SIZE(la_low_instr);
	la_low_prog.origin = -1;
}

static int la_load_program(const struct pio_program *prog, uint32_t *offset)
{
	if (!pio_can_add_program(la_pio, prog)) {
		return -ENOMEM;
	}

	*offset = pio_add_program(la_pio, prog);
	return 0;
}

static void la_pins_init(void)
{
	uint32_t pin;

	for (pin = LA_PIN_BASE; pin < (LA_PIN_BASE + LA_PIN_COUNT); pin++) {
		gpio_init(pin);
		gpio_set_dir(pin, GPIO_IN);
		gpio_pull_down(pin);
		gpio_set_function(pin, GPIO_FUNC_PIO0);
	}
}

static int la_init(void)
{
	int ret;
	size_t sm;

	if (!device_is_ready(la_pio_dev) || !device_is_ready(la_clk_dev) ||
	    !device_is_ready(la_dma_reset.dev)) {
		return -ENODEV;
	}

	ret = clock_control_get_rate(la_clk_dev,
				     (clock_control_subsys_t)RPI_PICO_CLKID_CLK_SYS,
				     &la_sysclk);
	if (ret != 0) {
		return ret;
	}

	ret = reset_line_toggle_dt(&la_dma_reset);
	if (ret != 0) {
		return ret;
	}

	la_pio = pio_rpi_pico_get_pio(la_pio_dev);
	ret = pio_rpi_pico_allocate_sm(la_pio_dev, &sm);
	if (ret != 0) {
		return ret;
	}
	la_sm = (uint32_t)sm;

	la_build_programs();
	ret = la_load_program(&la_free_prog, &la_off_free);
	if (ret != 0) {
		return ret;
	}
	ret = la_load_program(&la_high_prog, &la_off_high);
	if (ret != 0) {
		return ret;
	}
	ret = la_load_program(&la_low_prog, &la_off_low);
	if (ret != 0) {
		return ret;
	}

	la_pins_init();
	la_ready = true;
	return 0;
}

static void la_sm_prepare(uint32_t origin, uint32_t wrap_target, uint32_t wrap,
			  uint16_t div_int, uint8_t div_frac)
{
	pio_sm_config cfg = pio_get_default_sm_config();

	sm_config_set_wrap(&cfg, origin + wrap_target, origin + wrap);
	sm_config_set_in_pins(&cfg, LA_PIN_BASE);
	sm_config_set_jmp_pin(&cfg, LA_TRIGGER_PIN);
	sm_config_set_in_shift(&cfg, false, true, 32);
	sm_config_set_clkdiv_int_frac(&cfg, div_int, div_frac);

	pio_sm_set_enabled(la_pio, la_sm, false);
	pio_sm_init(la_pio, la_sm, origin, &cfg);
	pio_sm_set_consecutive_pindirs(la_pio, la_sm, LA_PIN_BASE, LA_PIN_COUNT, false);
	pio_sm_clear_fifos(la_pio, la_sm);
	pio_interrupt_clear(la_pio, 0);
}

static void la_dma_arm(uint32_t channel, uint32_t chain_to, uint32_t transfers, bool ring,
		       bool start)
{
	dma_channel_config cfg = dma_channel_get_default_config((uint)channel);

	channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
	channel_config_set_read_increment(&cfg, false);
	channel_config_set_write_increment(&cfg, true);
	channel_config_set_dreq(&cfg, pio_get_dreq(la_pio, la_sm, false));
	channel_config_set_chain_to(&cfg, (uint)chain_to);
	if (ring) {
		channel_config_set_ring(&cfg, true, LA_RING_BITS);
	}

	dma_channel_configure((uint)channel, &cfg, sample_buf, &la_pio->rxf[la_sm],
			      dma_encode_transfer_count(transfers), start);
}

/*
 * Edge captures run longer than one buffer. Two channels chain to each
 * other, and each programs a reload count of one buffer. A chain trigger
 * copies that reload into the live counter, so the ring keeps wrapping
 * until the state machine is stopped. The CPU does not restart DMA.
 */
static void la_dma_start_ring(void)
{
	la_dma_arm(LA_DMA_B, LA_DMA_A, LA_MAX_SAMPLES, true, false);
	la_dma_arm(LA_DMA_A, LA_DMA_B, LA_MAX_SAMPLES, true, true);
}

/* RP2350 keeps the transfer mode in the top 4 bits of the count register. */
static uint32_t la_xfer_remaining(const dma_channel_hw_t *hw)
{
	return hw->transfer_count & 0x0fffffffU;
}

static void la_dma_stop_free(uint32_t count, uint32_t *done)
{
	dma_channel_hw_t *hw = dma_channel_hw_addr(LA_DMA_A);
	uint32_t remaining;

	if (dma_channel_is_busy(LA_DMA_A)) {
		remaining = la_xfer_remaining(hw);
		*done = (remaining < count) ? (count - remaining) : 0U;
	} else {
		*done = count;
	}

	dma_channel_abort(LA_DMA_A);
	barrier_dsync_fence_full();
}

static int la_dma_stop_ring(uint32_t *done, uint32_t *write_addr)
{
	dma_channel_hw_t *ha = dma_channel_hw_addr(LA_DMA_A);
	dma_channel_hw_t *hb = dma_channel_hw_addr(LA_DMA_B);
	bool busy_a = dma_channel_is_busy(LA_DMA_A);
	bool busy_b = dma_channel_is_busy(LA_DMA_B);
	uint32_t rem_a = la_xfer_remaining(ha);
	uint32_t rem_b = la_xfer_remaining(hb);
	uint32_t base = (uint32_t)sample_buf;
	bool full;

	full = busy_b || (rem_b != LA_MAX_SAMPLES) || (!busy_a && (rem_a == 0U));
	if (full && (busy_b || (!busy_a && (rem_a == 0U)))) {
		*write_addr = hb->write_addr;
	} else {
		*write_addr = ha->write_addr;
	}

	if (full) {
		*done = LA_MAX_SAMPLES;
	} else {
		*done = (rem_a <= LA_MAX_SAMPLES) ? (LA_MAX_SAMPLES - rem_a) : 0U;
	}

	dma_channel_abort(LA_DMA_A);
	dma_channel_abort(LA_DMA_B);
	barrier_dsync_fence_full();

	if ((*write_addr < base) || (*write_addr >= (base + sizeof(sample_buf))) ||
	    ((*write_addr & 3U) != 0U)) {
		return -EIO;
	}

	return 0;
}

static int la_wait_until(bool edge, uint32_t timeout_ms)
{
	int64_t deadline = k_uptime_get() + (int64_t)timeout_ms;

	do {
		if (edge) {
			if (pio_interrupt_get(la_pio, 0)) {
				return 0;
			}
		} else if (!dma_channel_is_busy(LA_DMA_A)) {
			return 0;
		}

		k_msleep(1);
	} while (k_uptime_get() < deadline);

	return -ETIMEDOUT;
}

static void la_drain_rx(void)
{
	int64_t deadline = k_uptime_get() + 20;

	while (!pio_sm_is_rx_fifo_empty(la_pio, la_sm)) {
		if (k_uptime_get() > deadline) {
			break;
		}
		k_msleep(1);
	}
}

static int la_capture(const struct shell *sh, bool edge, bool level_high,
		      uint32_t rate, uint32_t count)
{
	uint16_t div_int;
	uint8_t div_frac;
	uint32_t actual;
	uint32_t cycles = edge ? 2U : 1U;
	uint32_t origin;
	uint32_t wrap_target;
	uint32_t wrap;
	uint32_t timeout_ms;
	uint32_t write_addr = 0U;
	uint32_t done = 0U;
	bool triggered = false;
	int ret;

	if (!la_ready) {
		return -ENODEV;
	}
	if ((count == 0U) || (count > LA_MAX_SAMPLES)) {
		return -EINVAL;
	}

	ret = la_divider(cycles, rate, &div_int, &div_frac, &actual);
	if (ret != 0) {
		return ret;
	}

	if (edge) {
		origin = level_high ? la_off_high : la_off_low;
		wrap_target = level_high ? 2U : 7U;
		wrap = level_high ? 3U : 7U;
		timeout_ms = LA_EDGE_TIMEOUT_MS;
	} else {
		origin = la_off_free;
		wrap_target = 0U;
		wrap = 0U;
		timeout_ms = (uint32_t)(((uint64_t)count * 2000ULL) / actual) + 1000U;
	}

	ret = k_mutex_lock(&la_lock, K_FOREVER);
	if (ret != 0) {
		return ret;
	}

	memset(sample_buf, 0, sizeof(sample_buf));
	la_last.valid = false;
	la_sm_prepare(origin, wrap_target, wrap, div_int, div_frac);

	if (edge) {
		pio_sm_put(la_pio, la_sm, count - 1U);
		la_dma_start_ring();
	} else {
		la_dma_arm(LA_DMA_A, LA_DMA_A, count, false, true);
	}

	pio_sm_set_enabled(la_pio, la_sm, true);
	ret = la_wait_until(edge, timeout_ms);
	if (edge && (ret == 0)) {
		triggered = true;
	}

	/*
	 * Stop the state machine before draining so the FIFO cannot grow
	 * while DMA copies the last words.
	 */
	pio_sm_set_enabled(la_pio, la_sm, false);
	if (edge) {
		int stop_ret;

		la_drain_rx();
		stop_ret = la_dma_stop_ring(&done, &write_addr);
		if (stop_ret != 0) {
			k_mutex_unlock(&la_lock);
			return stop_ret;
		}
	} else {
		la_dma_stop_free(count, &done);
	}

	if (edge) {
		uint32_t base = (uint32_t)sample_buf;
		uint32_t next;

		next = (write_addr - base) / sizeof(uint32_t);
		if (done >= LA_MAX_SAMPLES) {
			la_last.start = next;
			la_last.count = LA_MAX_SAMPLES;
		} else {
			la_last.start = 0U;
			la_last.count = done;
		}
		if (triggered && (la_last.count >= count)) {
			la_last.trigger_index = la_last.count - count;
		} else {
			la_last.trigger_index = UINT32_MAX;
		}
		la_last.post = count;
	} else if (ret == 0) {
		la_last.start = 0U;
		la_last.count = count;
		la_last.trigger_index = UINT32_MAX;
		la_last.post = 0U;
	} else {
		la_last.start = 0U;
		la_last.count = done;
		la_last.trigger_index = UINT32_MAX;
		la_last.post = 0U;
	}

	la_last.valid = (la_last.count > 0U);
	la_last.triggered = triggered && (la_last.trigger_index != UINT32_MAX);
	la_last.requested_hz = rate;
	la_last.actual_hz = actual;
	la_last.div_int = div_int;
	la_last.div_frac = div_frac;
	la_last.cycles = cycles;

	if (ret == -ETIMEDOUT) {
		LA_OUT(sh, "timed out");
	}
	la_print_result(sh);
	k_mutex_unlock(&la_lock);
	return ret;
}

static int cmd_info(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!la_ready) {
		shell_error(sh, "not initialized");
		return -ENODEV;
	}

	la_print_banner(sh);
	return 0;
}

static int cmd_free(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t rate;
	uint32_t count;
	int ret;

	ARG_UNUSED(argc);

	ret = la_parse_u32(argv[1], &rate);
	if (ret != 0) {
		shell_error(sh, "rate must be a positive integer");
		return ret;
	}
	ret = la_parse_u32(argv[2], &count);
	if ((ret != 0) || (count == 0U) || (count > LA_MAX_SAMPLES)) {
		shell_error(sh, "sample count must be 1..%u", LA_MAX_SAMPLES);
		return -EINVAL;
	}

	ret = la_capture(sh, false, false, rate, count);
	if ((ret != 0) && (ret != -ETIMEDOUT)) {
		shell_error(sh, "capture failed (%d)", ret);
	}

	return ret;
}

static int cmd_edge(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t rate;
	uint32_t post;
	uint32_t level;
	int ret;

	ARG_UNUSED(argc);

	ret = la_parse_u32(argv[1], &rate);
	if (ret != 0) {
		shell_error(sh, "rate must be a positive integer");
		return ret;
	}
	ret = la_parse_u32(argv[2], &post);
	if ((ret != 0) || (post == 0U) || (post > LA_MAX_SAMPLES)) {
		shell_error(sh, "post count must be 1..%u", LA_MAX_SAMPLES);
		return -EINVAL;
	}
	ret = la_parse_u32(argv[3], &level);
	if ((ret != 0) || (level > 1U)) {
		shell_error(sh, "level must be 0 or 1");
		return -EINVAL;
	}

	ret = la_capture(sh, true, level == 1U, rate, post);
	if ((ret != 0) && (ret != -ETIMEDOUT)) {
		shell_error(sh, "capture failed (%d)", ret);
	}

	return ret;
}

static int cmd_dump(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t count;
	uint32_t i;
	int ret;

	ret = k_mutex_lock(&la_lock, K_FOREVER);
	if (ret != 0) {
		return ret;
	}
	if (!la_last.valid) {
		k_mutex_unlock(&la_lock);
		shell_error(sh, "no capture");
		return -ENODATA;
	}

	count = MIN(la_last.count, LA_DUMP_MAX);
	if (argc > 1) {
		ret = la_parse_u32(argv[1], &count);
		if (ret != 0) {
			k_mutex_unlock(&la_lock);
			shell_error(sh, "count must be a positive integer");
			return ret;
		}
	}
	if (count > la_last.count) {
		count = la_last.count;
	}
	if (count > LA_DUMP_MAX) {
		count = LA_DUMP_MAX;
	}

	for (i = 0U; i < count; i++) {
		shell_print(sh, "sample %u 0x%08x", i, la_at(i));
	}

	k_mutex_unlock(&la_lock);
	return 0;
}

static int cmd_la(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "info");
	shell_print(sh, "free <hz> <samples>");
	shell_print(sh, "edge <hz> <post> <level 0|1>");
	shell_print(sh, "dump [count]");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(la_cmds,
	SHELL_CMD(info, NULL, "Print clock and pin map", cmd_info),
	SHELL_CMD_ARG(free, NULL, "Free-run capture: free <hz> <samples>", cmd_free, 3, 0),
	SHELL_CMD_ARG(edge, NULL,
		      "Edge capture: edge <hz> <post> <level>", cmd_edge, 4, 0),
	SHELL_CMD_ARG(dump, NULL, "Print samples from the last capture", cmd_dump, 1, 1),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(la, &la_cmds, "PIO logic analyzer", cmd_la);

int main(void)
{
	int ret;

	ret = la_init();
	if (ret != 0) {
		printk("logic analyzer init failed: %d\n", ret);
		return 0;
	}

	la_print_banner(NULL);
	ret = la_capture(NULL, false, false, 1000000U, 64U);
	if (ret != 0) {
		printk("boot capture failed: %d\n", ret);
		return 0;
	}

	printk("boot capture complete\n");
	return 0;
}
