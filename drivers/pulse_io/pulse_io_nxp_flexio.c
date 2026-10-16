/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_flexio_pulse_io

/*
 * Each channel owns one FlexIO shifter and one timer. The timer runs in
 * dual 8-bit baud mode and clocks the shifter once per tick, so the
 * channel transmits or captures one bit per tick: the waveform is a
 * bitstream that the interrupt handler refills or drains 32 ticks at a
 * time. Symbols and cells are expanded to that bitstream on the fly, so
 * any level and duration quantized to the tick can be generated.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/misc/nxp_flexio/nxp_flexio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/pulse_io.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <fsl_flexio.h>

LOG_MODULE_REGISTER(pulse_io_nxp_flexio, CONFIG_PULSE_IO_LOG_LEVEL);

#define FPIO_WORD_BITS 32U

/* Dual 8-bit baud mode: (CMP[15:8] + 1) / 2 bits per word, a tick of 2 * (CMP[7:0] + 1) clocks */
#define FPIO_TIMCMP(div) ((((FPIO_WORD_BITS * 2U) - 1U) << 8) | (div))
#define FPIO_DIV_MAX     255U

#define FPIO_DURATION_MAX BIT_MASK(31)

enum fpio_state {
	FPIO_FREE,
	FPIO_OPEN,
	FPIO_READY,
	FPIO_ACTIVE,
};

enum fpio_tx_phase {
	/* Data words are being written */
	FPIO_TX_DATA,
	/* The last data word is written; an idle word follows it */
	FPIO_TX_DRAIN,
	/* The idle word is written; its load ends the last data word */
	FPIO_TX_FINISH,
};

struct fpio_channel {
	uint8_t index;
	uint8_t pin;
	uint8_t shifter;
	uint8_t timer;
	enum fpio_state state;
	struct pulse_io_config cfg;
	uint8_t div;
	uint32_t resolution_hz;
	struct k_sem done;
	int result;

	/* Transmission */
	const struct pulse_symbol *syms;
	const struct pulse_cell *cells;
	size_t count;
	size_t pos;
	uint32_t rem;
	uint8_t level;
	bool cell_low;
	enum fpio_tx_phase phase;

	/* Capture */
	struct pulse_symbol *rx_buf;
	size_t rx_cap;
	size_t rx_count;
	uint32_t rx_run;
	uint8_t rx_level;
	bool rx_overflow;
};

struct fpio_config {
	const struct device *flexio_dev;
	FLEXIO_Type *base;
	const struct pinctrl_dev_config *pcfg;
	const struct nxp_flexio_child *child;
	const uint8_t *pins;
	uint8_t num_channels;
};

struct fpio_data {
	struct k_spinlock lock;
	uint32_t clock_hz;
	struct fpio_channel *channels;
};

static struct fpio_channel *fpio_to_channel(const struct device *dev,
					    struct pulse_io_channel *handle)
{
	const struct fpio_config *config = dev->config;
	struct fpio_data *data = dev->data;
	uintptr_t idx = (uintptr_t)handle - 1U;

	if (idx >= config->num_channels) {
		return NULL;
	}
	return &data->channels[idx];
}

static uint32_t fpio_max_rate_hz(const struct device *dev)
{
	struct fpio_data *data = dev->data;

	return MIN(data->clock_hz / 2U, (uint32_t)CONFIG_PULSE_IO_NXP_FLEXIO_MAX_TICK_RATE);
}

/* Divider giving the rate nearest to resolution_hz without exceeding the maximum */
static uint8_t fpio_divider(const struct device *dev, uint32_t resolution_hz)
{
	struct fpio_data *data = dev->data;
	uint64_t ticks2 = DIV_ROUND_CLOSEST((uint64_t)data->clock_hz, 2ULL * resolution_hz);
	uint64_t min_ticks2 = DIV_ROUND_UP((uint64_t)data->clock_hz, 2ULL * fpio_max_rate_hz(dev));

	return (uint8_t)(CLAMP(MAX(ticks2, min_ticks2), 1U, FPIO_DIV_MAX + 1U) - 1U);
}

static void fpio_irq_set(FLEXIO_Type *base, uint8_t shifter, bool enable)
{
	if (enable) {
		base->SHIFTSIEN |= BIT(shifter);
		base->SHIFTEIEN |= BIT(shifter);
	} else {
		base->SHIFTSIEN &= ~BIT(shifter);
		base->SHIFTEIEN &= ~BIT(shifter);
	}
}

static void fpio_timer_stop(FLEXIO_Type *base, struct fpio_channel *ch)
{
	base->TIMCTL[ch->timer] &= ~FLEXIO_TIMCTL_TIMOD_MASK;
}

/* Next level and the number of ticks it lasts; false at the end of the stream */
static bool fpio_tx_next_run(struct fpio_channel *ch)
{
	while (ch->rem == 0U) {
		if (ch->pos >= ch->count) {
			return false;
		}
		if (ch->syms != NULL) {
			ch->rem = ch->syms[ch->pos].duration;
			ch->level = ch->syms[ch->pos].level;
			ch->pos++;
		} else if (!ch->cell_low) {
			ch->rem = ch->cells[ch->pos].duty;
			ch->level = 1U;
			ch->cell_low = true;
		} else {
			ch->rem = ch->cfg.cell_period_ticks - ch->cells[ch->pos].duty;
			ch->level = 0U;
			ch->cell_low = false;
			ch->pos++;
		}
	}

	return true;
}

/*
 * Pack the next 32 ticks, first tick in bit 0. A set bit is the active
 * level: the shifter pin polarity turns it into the configured levels.
 */
static uint32_t fpio_tx_next_word(struct fpio_channel *ch, bool *last)
{
	uint8_t active = ch->cfg.idle_high ? 0U : 1U;
	uint32_t word = 0;
	uint32_t bit = 0;

	while (bit < FPIO_WORD_BITS) {
		uint32_t n;

		if (!fpio_tx_next_run(ch)) {
			*last = true;
			return word;
		}
		n = MIN(ch->rem, FPIO_WORD_BITS - bit);
		if (ch->level == active) {
			word |= (uint32_t)(BIT64(n) - 1U) << bit;
		}
		bit += n;
		ch->rem -= n;
	}

	*last = (ch->rem == 0U) && (ch->pos >= ch->count);
	return word;
}

static void fpio_finish(const struct device *dev, struct fpio_channel *ch, int result)
{
	const struct fpio_config *config = dev->config;

	fpio_timer_stop(config->base, ch);
	fpio_irq_set(config->base, ch->shifter, false);
	ch->state = FPIO_READY;
	ch->result = result;
	k_sem_give(&ch->done);
}

static void fpio_tx_isr(const struct device *dev, struct fpio_channel *ch)
{
	const struct fpio_config *config = dev->config;
	FLEXIO_Type *base = config->base;
	bool last = false;

	/* An empty buffer was loaded into the shifter before the data ended */
	if (((base->SHIFTERR & BIT(ch->shifter)) != 0U) && (ch->phase != FPIO_TX_FINISH)) {
		base->SHIFTERR = BIT(ch->shifter);
		fpio_finish(dev, ch, -EIO);
		return;
	}
	if ((base->SHIFTSTAT & BIT(ch->shifter)) == 0U) {
		return;
	}

	switch (ch->phase) {
	case FPIO_TX_DATA:
		base->SHIFTBUF[ch->shifter] = fpio_tx_next_word(ch, &last);
		if (last) {
			ch->phase = FPIO_TX_DRAIN;
		}
		break;
	case FPIO_TX_DRAIN:
		/* Idle bits, also replayed should the handler be late to stop */
		base->SHIFTBUF[ch->shifter] = 0U;
		ch->phase = FPIO_TX_FINISH;
		break;
	default:
		base->SHIFTERR = BIT(ch->shifter);
		fpio_finish(dev, ch, 0);
		break;
	}
}

static void fpio_rx_store(struct fpio_channel *ch)
{
	if (ch->rx_run == 0U) {
		return;
	}
	if (ch->rx_count < ch->rx_cap) {
		ch->rx_buf[ch->rx_count++] = (struct pulse_symbol){
			.duration = MIN(ch->rx_run, FPIO_DURATION_MAX),
			.level = ch->rx_level,
		};
	} else {
		ch->rx_overflow = true;
	}
}

static void fpio_rx_isr(const struct device *dev, struct fpio_channel *ch)
{
	const struct fpio_config *config = dev->config;
	FLEXIO_Type *base = config->base;
	uint8_t idle = ch->cfg.idle_high ? 1U : 0U;
	uint32_t word;

	/* A word completed before the previous one was read */
	if ((base->SHIFTERR & BIT(ch->shifter)) != 0U) {
		base->SHIFTERR = BIT(ch->shifter);
		fpio_finish(dev, ch, -EIO);
		return;
	}
	if ((base->SHIFTSTAT & BIT(ch->shifter)) == 0U) {
		return;
	}

	/* Reading the buffer clears the status flag; the first sample is bit 0 */
	word = base->SHIFTBUF[ch->shifter];

	for (uint32_t bit = 0; bit < FPIO_WORD_BITS; bit++) {
		uint8_t level = (uint8_t)((word >> bit) & 1U);

		if (level != ch->rx_level) {
			fpio_rx_store(ch);
			ch->rx_level = level;
			ch->rx_run = 0U;
		}
		ch->rx_run++;

		if (ch->rx_overflow) {
			fpio_finish(dev, ch, -ENOMEM);
			return;
		}
		/* Without an idle threshold the capture ends when the buffer is full */
		if ((ch->cfg.rx_idle_threshold_ticks == 0U) && (ch->rx_count == ch->rx_cap)) {
			fpio_finish(dev, ch, 0);
			return;
		}
		if ((ch->rx_level == idle) && (ch->cfg.rx_idle_threshold_ticks != 0U) &&
		    (ch->rx_run >= ch->cfg.rx_idle_threshold_ticks)) {
			fpio_finish(dev, ch, 0);
			return;
		}
	}
}

static int fpio_isr(void *user_data)
{
	const struct device *dev = user_data;
	const struct fpio_config *config = dev->config;
	struct fpio_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	for (uint8_t i = 0; i < config->num_channels; i++) {
		struct fpio_channel *ch = &data->channels[i];

		if (ch->state != FPIO_ACTIVE) {
			continue;
		}
		if (ch->cfg.dir == PULSE_IO_DIR_TX) {
			fpio_tx_isr(dev, ch);
		} else {
			fpio_rx_isr(dev, ch);
		}
	}

	k_spin_unlock(&data->lock, key);

	return 0;
}

static int fpio_get_capabilities(const struct device *dev, struct pulse_io_caps *caps)
{
	const struct fpio_config *config = dev->config;
	struct fpio_data *data = dev->data;
	uint32_t channels = BIT_MASK(config->num_channels);

	*caps = (struct pulse_io_caps){
		.modes = PULSE_IO_MODE_SYMBOL | PULSE_IO_MODE_CELL,
		.supports_tx = true,
		.supports_rx = true,
		.min_tick_ns = DIV_ROUND_UP(NSEC_PER_SEC, fpio_max_rate_hz(dev)),
		.max_tick_ns = (uint32_t)(((uint64_t)NSEC_PER_SEC * 2U * (FPIO_DIV_MAX + 1U)) /
					  data->clock_hz),
		.max_duration_ticks = FPIO_DURATION_MAX,
		.rx_idle_threshold_max = FPIO_DURATION_MAX,
		.tx_channel_mask = channels,
		.rx_channel_mask = channels,
		.tx_max_streaming = SIZE_MAX,
		.cell_duty_bits = 16,
		.num_channels = config->num_channels,
		.tx_sync_multi_channel = true,
	};

	return 0;
}

static int fpio_channel_get(const struct device *dev, uint8_t channel_idx,
			    struct pulse_io_channel **chan)
{
	const struct fpio_config *config = dev->config;
	struct fpio_data *data = dev->data;
	k_spinlock_key_t key;
	int ret = 0;

	if (channel_idx >= config->num_channels) {
		return -ENODEV;
	}

	key = k_spin_lock(&data->lock);
	if (data->channels[channel_idx].state != FPIO_FREE) {
		ret = -EBUSY;
	} else {
		data->channels[channel_idx].state = FPIO_OPEN;
	}
	k_spin_unlock(&data->lock, key);

	if (ret == 0) {
		*chan = (struct pulse_io_channel *)(uintptr_t)(channel_idx + 1U);
	}
	return ret;
}

static void fpio_halt(const struct device *dev, struct fpio_channel *ch)
{
	const struct fpio_config *config = dev->config;
	struct fpio_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	fpio_timer_stop(config->base, ch);
	fpio_irq_set(config->base, ch->shifter, false);
	config->base->SHIFTERR = BIT(ch->shifter);
	if (ch->state == FPIO_ACTIVE) {
		ch->state = FPIO_READY;
	}
	k_spin_unlock(&data->lock, key);
}

static int fpio_channel_release(const struct device *dev, struct pulse_io_channel *chan)
{
	const struct fpio_config *config = dev->config;
	struct fpio_channel *ch = fpio_to_channel(dev, chan);

	if ((ch == NULL) || (ch->state == FPIO_FREE)) {
		return -EINVAL;
	}

	fpio_halt(dev, ch);
	config->base->SHIFTCTL[ch->shifter] = 0U;
	config->base->TIMCTL[ch->timer] = 0U;
	ch->state = FPIO_FREE;

	return 0;
}

static void fpio_setup_tx(const struct device *dev, struct fpio_channel *ch)
{
	const struct fpio_config *config = dev->config;
	flexio_shifter_config_t shifter = {
		.timerSelect = ch->timer,
		.timerPolarity = kFLEXIO_ShifterTimerPolarityOnPositive,
		.pinConfig = kFLEXIO_PinConfigOutput,
		.pinSelect = ch->pin,
		/* The shifter idles with zeros, which the polarity maps to the idle level */
		.pinPolarity = ch->cfg.idle_high ? kFLEXIO_PinActiveLow : kFLEXIO_PinActiveHigh,
		.shifterMode = kFLEXIO_ShifterModeTransmit,
		.inputSource = kFLEXIO_ShifterInputFromPin,
		.shifterStop = kFLEXIO_ShifterStopBitDisable,
		.shifterStart = kFLEXIO_ShifterStartBitDisabledLoadDataOnEnable,
	};

	FLEXIO_SetShifterConfig(config->base, ch->shifter, &shifter);
}

static void fpio_setup_rx(const struct device *dev, struct fpio_channel *ch)
{
	const struct fpio_config *config = dev->config;
	flexio_shifter_config_t shifter = {
		.timerSelect = ch->timer,
		/* Sample in the middle of each tick */
		.timerPolarity = kFLEXIO_ShifterTimerPolarityOnNegitive,
		.pinConfig = kFLEXIO_PinConfigOutputDisabled,
		.pinSelect = ch->pin,
		.pinPolarity = kFLEXIO_PinActiveHigh,
		.shifterMode = kFLEXIO_ShifterModeReceive,
		.inputSource = kFLEXIO_ShifterInputFromPin,
		.shifterStop = kFLEXIO_ShifterStopBitDisable,
		.shifterStart = kFLEXIO_ShifterStartBitDisabledLoadDataOnEnable,
	};

	FLEXIO_SetShifterConfig(config->base, ch->shifter, &shifter);
}

/*
 * Arm the channel timer; it runs until the transfer ends. A transmit
 * timer starts when the buffer of trigger_shifter is written.
 */
static void fpio_timer_start(const struct device *dev, struct fpio_channel *ch,
			     uint8_t trigger_shifter)
{
	const struct fpio_config *config = dev->config;
	flexio_timer_config_t timer = {
		.triggerSource = kFLEXIO_TimerTriggerSourceInternal,
		.pinConfig = kFLEXIO_PinConfigOutputDisabled,
		.pinSelect = ch->pin,
		.pinPolarity = kFLEXIO_PinActiveHigh,
		.timerMode = kFLEXIO_TimerModeDual8BitBaudBit,
		.timerOutput = kFLEXIO_TimerOutputOneNotAffectedByReset,
		.timerDecrement = kFLEXIO_TimerDecSrcOnFlexIOClockShiftTimerOutput,
		.timerReset = kFLEXIO_TimerResetNever,
		.timerDisable = kFLEXIO_TimerDisableNever,
		.timerStop = kFLEXIO_TimerStopBitDisabled,
		.timerStart = kFLEXIO_TimerStartBitDisabled,
		.timerCompare = FPIO_TIMCMP(ch->div),
	};

	if (ch->cfg.dir == PULSE_IO_DIR_TX) {
		timer.triggerSelect = FLEXIO_TIMER_TRIGGER_SEL_SHIFTnSTAT(trigger_shifter);
		timer.triggerPolarity = kFLEXIO_TimerTriggerPolarityActiveLow;
		timer.timerEnable = kFLEXIO_TimerEnableOnTriggerHigh;
	} else {
		/* Start on the first edge on the pin */
		timer.triggerSelect = FLEXIO_TIMER_TRIGGER_SEL_PININPUT(ch->pin);
		timer.triggerPolarity = kFLEXIO_TimerTriggerPolarityActiveHigh;
		timer.timerEnable = kFLEXIO_TimerEnableOnTriggerBothEdge;
	}

	FLEXIO_SetTimerConfig(config->base, ch->timer, &timer);
}

static int fpio_channel_configure(const struct device *dev, struct pulse_io_channel *chan,
				  const struct pulse_io_config *cfg)
{
	struct fpio_data *data = dev->data;
	struct fpio_channel *ch = fpio_to_channel(dev, chan);

	if ((ch == NULL) || (cfg == NULL)) {
		return -EINVAL;
	}
	if ((ch->state != FPIO_OPEN) && (ch->state != FPIO_READY)) {
		return -EINVAL;
	}
	if ((cfg->mode != PULSE_IO_MODE_SYMBOL) && (cfg->mode != PULSE_IO_MODE_CELL)) {
		return -ENOTSUP;
	}
	if ((cfg->dir == PULSE_IO_DIR_RX) && (cfg->mode != PULSE_IO_MODE_SYMBOL)) {
		return -ENOTSUP;
	}
	if (cfg->carrier_en || cfg->rx_carrier_demod || (cfg->rx_filter_ticks != 0U)) {
		return -ENOTSUP;
	}
	if ((cfg->resolution_hz == 0U) ||
	    ((cfg->mode == PULSE_IO_MODE_CELL) &&
	     ((cfg->cell_period_ticks == 0U) || (cfg->cell_period_ticks > UINT16_MAX))) ||
	    (cfg->rx_idle_threshold_ticks > FPIO_DURATION_MAX)) {
		return -EINVAL;
	}

	ch->cfg = *cfg;
	ch->div = fpio_divider(dev, cfg->resolution_hz);
	ch->resolution_hz = data->clock_hz / (2U * (ch->div + 1U));
	if (ch->resolution_hz != cfg->resolution_hz) {
		LOG_DBG("channel %u tick rate set to %u Hz", ch->index, ch->resolution_hz);
	}

	fpio_halt(dev, ch);
	if (cfg->dir == PULSE_IO_DIR_TX) {
		fpio_setup_tx(dev, ch);
	} else {
		fpio_setup_rx(dev, ch);
	}
	ch->state = FPIO_READY;

	return 0;
}

static int fpio_channel_get_resolution(const struct device *dev, struct pulse_io_channel *chan,
				       uint32_t *resolution_hz)
{
	struct fpio_channel *ch = fpio_to_channel(dev, chan);

	if ((ch == NULL) || (resolution_hz == NULL) ||
	    ((ch->state != FPIO_READY) && (ch->state != FPIO_ACTIVE))) {
		return -EINVAL;
	}

	*resolution_hz = ch->resolution_hz;
	return 0;
}

static int fpio_tx_check(const struct fpio_channel *ch, const struct pulse_io_tx_req *req)
{
	if ((ch == NULL) || (req == NULL) || (req->symbols == NULL) || (req->count == 0U)) {
		return -EINVAL;
	}
	if ((ch->state != FPIO_READY) || (ch->cfg.dir != PULSE_IO_DIR_TX)) {
		return -EINVAL;
	}
	if (req->loop_count != 0U) {
		return -ENOTSUP;
	}
	if (ch->cfg.mode == PULSE_IO_MODE_CELL) {
		for (size_t i = 0; i < req->count; i++) {
			if (req->cells[i].duty > ch->cfg.cell_period_ticks) {
				return -EINVAL;
			}
		}
	}

	return 0;
}

/* Load a transmission and arm its timer; returns the first word to write */
static uint32_t fpio_tx_prepare(const struct device *dev, struct fpio_channel *ch,
				const struct pulse_io_tx_req *req, uint8_t trigger_shifter)
{
	const struct fpio_config *config = dev->config;
	bool last = false;
	uint32_t word;

	ch->syms = (ch->cfg.mode == PULSE_IO_MODE_SYMBOL) ? req->symbols : NULL;
	ch->cells = (ch->cfg.mode == PULSE_IO_MODE_CELL) ? req->cells : NULL;
	ch->count = req->count;
	ch->pos = 0;
	ch->rem = 0;
	ch->cell_low = false;
	ch->result = 0;

	word = fpio_tx_next_word(ch, &last);
	ch->phase = last ? FPIO_TX_DRAIN : FPIO_TX_DATA;
	ch->state = FPIO_ACTIVE;
	config->base->SHIFTERR = BIT(ch->shifter);
	fpio_timer_start(dev, ch, trigger_shifter);

	return word;
}

static int fpio_transmit_sync(const struct device *dev, struct pulse_io_channel *chan,
			      const struct pulse_io_tx_req *req, k_timeout_t timeout)
{
	const struct fpio_config *config = dev->config;
	struct fpio_data *data = dev->data;
	struct fpio_channel *ch = fpio_to_channel(dev, chan);
	FLEXIO_Type *base = config->base;
	k_spinlock_key_t key;
	uint32_t word;
	int ret;

	ret = fpio_tx_check(ch, req);
	if (ret != 0) {
		return ret;
	}
	k_sem_reset(&ch->done);

	key = k_spin_lock(&data->lock);
	word = fpio_tx_prepare(dev, ch, req, ch->shifter);
	/* Writing the buffer enables the timer */
	base->SHIFTBUF[ch->shifter] = word;
	fpio_irq_set(base, ch->shifter, true);
	k_spin_unlock(&data->lock, key);

	if (k_sem_take(&ch->done, timeout) != 0) {
		fpio_halt(dev, ch);
		k_sem_reset(&ch->done);
		return -ETIMEDOUT;
	}

	return ch->result;
}

static int fpio_transmit_group_sync(const struct device *dev,
				    const struct pulse_io_tx_group_entry *entries, size_t count,
				    uint32_t flags, k_timeout_t timeout)
{
	const struct fpio_config *config = dev->config;
	struct fpio_data *data = dev->data;
	struct fpio_channel *chans[PULSE_IO_TX_GROUP_MAX];
	uint32_t words[PULSE_IO_TX_GROUP_MAX];
	k_timepoint_t end = sys_timepoint_calc(timeout);
	FLEXIO_Type *base = config->base;
	uint8_t trigger;
	k_spinlock_key_t key;
	int first_err = 0;
	uint32_t seen = 0;

	ARG_UNUSED(flags);

	for (size_t i = 0; i < count; i++) {
		struct fpio_channel *ch = fpio_to_channel(dev, entries[i].chan);
		int ret = fpio_tx_check(ch, &entries[i].req);

		if (ret != 0) {
			return ret;
		}
		if ((seen & BIT(ch->index)) != 0U) {
			return -EINVAL;
		}
		seen |= BIT(ch->index);
		chans[i] = ch;
	}
	for (size_t i = 0; i < count; i++) {
		k_sem_reset(&chans[i]->done);
	}

	/*
	 * Every timer waits for the buffer of the last shifter, so writing
	 * that buffer starts all the channels on the same FlexIO clock edge.
	 */
	trigger = chans[count - 1U]->shifter;

	key = k_spin_lock(&data->lock);
	for (size_t i = 0; i < count; i++) {
		words[i] = fpio_tx_prepare(dev, chans[i], &entries[i].req, trigger);
	}
	for (size_t i = 0; i < count; i++) {
		base->SHIFTBUF[chans[i]->shifter] = words[i];
	}
	for (size_t i = 0; i < count; i++) {
		fpio_irq_set(base, chans[i]->shifter, true);
	}
	k_spin_unlock(&data->lock, key);

	for (size_t i = 0; i < count; i++) {
		struct fpio_channel *ch = chans[i];
		int ret;

		if (k_sem_take(&ch->done, sys_timepoint_timeout(end)) != 0) {
			fpio_halt(dev, ch);
			k_sem_reset(&ch->done);
			ret = -ETIMEDOUT;
		} else {
			ret = ch->result;
		}
		if ((ret != 0) && (first_err == 0)) {
			first_err = ret;
		}
	}

	return first_err;
}

static int fpio_receive_sync(const struct device *dev, struct pulse_io_channel *chan,
			     const struct pulse_io_rx_req *req, size_t *count, k_timeout_t timeout)
{
	const struct fpio_config *config = dev->config;
	struct fpio_data *data = dev->data;
	struct fpio_channel *ch = fpio_to_channel(dev, chan);
	FLEXIO_Type *base = config->base;
	k_spinlock_key_t key;

	if ((ch == NULL) || (req == NULL) || (req->symbols == NULL) || (req->capacity == 0U) ||
	    (count == NULL)) {
		return -EINVAL;
	}
	if ((ch->state != FPIO_READY) || (ch->cfg.dir != PULSE_IO_DIR_RX)) {
		return -EINVAL;
	}

	ch->rx_buf = req->symbols;
	ch->rx_cap = req->capacity;
	ch->rx_count = 0;
	ch->rx_overflow = false;
	/* The timer starts on the first edge, which leaves the idle level */
	ch->rx_level = ch->cfg.idle_high ? 0U : 1U;
	ch->rx_run = 0;
	ch->result = 0;
	k_sem_reset(&ch->done);

	key = k_spin_lock(&data->lock);
	ch->state = FPIO_ACTIVE;
	(void)base->SHIFTBUF[ch->shifter];
	base->SHIFTERR = BIT(ch->shifter);
	/* Restart the shift register from a clean state */
	fpio_setup_rx(dev, ch);
	fpio_timer_start(dev, ch, ch->shifter);
	fpio_irq_set(base, ch->shifter, true);
	k_spin_unlock(&data->lock, key);

	if (k_sem_take(&ch->done, timeout) != 0) {
		fpio_halt(dev, ch);
		k_sem_reset(&ch->done);
		return -ETIMEDOUT;
	}

	*count = ch->rx_count;
	return ch->result;
}

static int fpio_stop(const struct device *dev, struct pulse_io_channel *chan)
{
	struct fpio_data *data = dev->data;
	struct fpio_channel *ch = fpio_to_channel(dev, chan);
	k_spinlock_key_t key;
	bool active;

	if ((ch == NULL) || (ch->state == FPIO_FREE)) {
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);
	active = (ch->state == FPIO_ACTIVE);
	k_spin_unlock(&data->lock, key);

	if (active) {
		fpio_halt(dev, ch);
		ch->result = -ECANCELED;
		k_sem_give(&ch->done);
	}

	return 0;
}

static int fpio_init(const struct device *dev)
{
	const struct fpio_config *config = dev->config;
	struct fpio_data *data = dev->data;
	int ret;

	if (!device_is_ready(config->flexio_dev)) {
		return -ENODEV;
	}

	ret = nxp_flexio_get_rate(config->flexio_dev, &data->clock_hz);
	if ((ret != 0) || (data->clock_hz == 0U)) {
		LOG_ERR("cannot get the FlexIO clock rate (%d)", ret);
		return (ret != 0) ? ret : -EINVAL;
	}

	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret != 0) {
		return ret;
	}

	ret = nxp_flexio_child_attach(config->flexio_dev, config->child);
	if (ret != 0) {
		LOG_ERR("not enough FlexIO shifters and timers for %u channels",
			config->num_channels);
		return ret;
	}

	for (uint8_t i = 0; i < config->num_channels; i++) {
		struct fpio_channel *ch = &data->channels[i];

		ch->index = i;
		ch->pin = config->pins[i];
		ch->shifter = config->child->res.shifter_index[i];
		ch->timer = config->child->res.timer_index[i];
		ch->state = FPIO_FREE;
		k_sem_init(&ch->done, 0, 1);
	}

	return 0;
}

static DEVICE_API(pulse_io, fpio_api) = {
	.get_capabilities = fpio_get_capabilities,
	.channel_get = fpio_channel_get,
	.channel_release = fpio_channel_release,
	.channel_configure = fpio_channel_configure,
	.transmit_sync = fpio_transmit_sync,
	.receive_sync = fpio_receive_sync,
	.stop = fpio_stop,
	.channel_get_resolution = fpio_channel_get_resolution,
	.transmit_group_sync = fpio_transmit_group_sync,
};

#define FPIO_NUM_CHANNELS(n) DT_INST_PROP_LEN(n, pin_ids)

#define FPIO_INIT(n)                                                                               \
	BUILD_ASSERT(FPIO_NUM_CHANNELS(n) <= 32, "at most 32 channels");                           \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	static const uint8_t fpio_pins_##n[] = DT_INST_PROP(n, pin_ids);                           \
	static uint8_t fpio_shifters_##n[FPIO_NUM_CHANNELS(n)];                                    \
	static uint8_t fpio_timers_##n[FPIO_NUM_CHANNELS(n)];                                      \
	static struct fpio_channel fpio_channels_##n[FPIO_NUM_CHANNELS(n)];                        \
	static const struct nxp_flexio_child fpio_child_##n = {                                    \
		.isr = fpio_isr,                                                                   \
		.user_data = (void *)DEVICE_DT_INST_GET(n),                                        \
		.res = {                                                                           \
			.shifter_index = fpio_shifters_##n,                                        \
			.shifter_count = FPIO_NUM_CHANNELS(n),                                     \
			.timer_index = fpio_timers_##n,                                            \
			.timer_count = FPIO_NUM_CHANNELS(n),                                       \
		},                                                                                 \
	};                                                                                         \
	static const struct fpio_config fpio_config_##n = {                                        \
		.flexio_dev = DEVICE_DT_GET(DT_INST_PARENT(n)),                                    \
		.base = (FLEXIO_Type *)DT_REG_ADDR(DT_INST_PARENT(n)),                             \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                         \
		.child = &fpio_child_##n,                                                          \
		.pins = fpio_pins_##n,                                                             \
		.num_channels = FPIO_NUM_CHANNELS(n),                                              \
	};                                                                                         \
	static struct fpio_data fpio_data_##n = {                                                  \
		.channels = fpio_channels_##n,                                                     \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, fpio_init, NULL, &fpio_data_##n, &fpio_config_##n, POST_KERNEL,   \
			      CONFIG_PULSE_IO_INIT_PRIORITY, &fpio_api);

DT_INST_FOREACH_STATUS_OKAY(FPIO_INIT)
