/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Pulse IO on the channels of an STM32 timer: CELL mode transmit, and the
 * receive that follows a transmit on a half duplex channel (TX_RX), through
 * RTIO.
 *
 * The channels run in PWM mode with preloaded compare registers. Each update
 * event loads the duty cycles of the current period from the preload
 * registers and requests a DMA burst that writes the duty cycles of the
 * following period into them through the DMA address register (DMAR), one
 * word per compare register from the first to the last transmitting channel.
 * The first two duty cycles are written by the CPU before the counter
 * starts, the DMA provides the others, and an extra cell at the idle level
 * ends every frame. The channels of a transmit start on the same update
 * event. A cell starts at the level opposite to the idle level of its
 * channel: PWM mode 1 when the line idles low, PWM mode 2 when it idles high.
 *
 * A receive turns the channel into an input capture on both edges, whose
 * own DMA request stores the counter at each edge, and ends once no edge
 * arrived for the idle threshold of the channel, or the buffer is full.
 */

#define DT_DRV_COMPAT st_stm32_pulse_io

#include <stddef.h>

#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_stm32.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/pulse_io.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/mpsc_lockfree.h>
#include <zephyr/sys/util.h>

#include <stm32_ll_tim.h>

LOG_MODULE_REGISTER(pulse_io_stm32_timer, CONFIG_PULSE_IO_LOG_LEVEL);

/* A receive captures both edges, which the timers of some series cannot */
#if defined(CONFIG_RTIO) && defined(LL_TIM_IC_POLARITY_BOTHEDGE)
#define STM32_PIO_RX 1
#else
#define STM32_PIO_RX 0
#endif

#define STM32_PIO_NUM_CHANNELS 4U

/* The DMA buffers are flushed from or invalidated in the data cache around each transfer */
#if defined(CONFIG_DCACHE)
#define STM32_PIO_BUF_ALIGN CONFIG_DCACHE_LINE_SIZE
#else
#define STM32_PIO_BUF_ALIGN sizeof(uint32_t)
#endif

static const uint32_t ll_channels[STM32_PIO_NUM_CHANNELS] = {
	LL_TIM_CHANNEL_CH1,
	LL_TIM_CHANNEL_CH2,
	LL_TIM_CHANNEL_CH3,
	LL_TIM_CHANNEL_CH4,
};

/* DMA driven by the capture/compare request of a channel, for its receive */
struct stm32_pio_rx_dma {
	/* NULL for a channel that cannot receive */
	const struct device *dev;
	uint32_t channel;
	uint32_t slot;
	uint32_t priority;
};

struct stm32_pio_config {
	TIM_TypeDef *timer;
	const struct stm32_pclken *pclken;
	size_t pclk_len;
	const struct pinctrl_dev_config *pcfg;
	const struct device *dma_dev;
	uint32_t dma_channel;
	uint32_t dma_slot;
	uint32_t dma_priority;
	/* buf_cells cells of up to STM32_PIO_NUM_CHANNELS words */
	uint32_t *buf;
	size_t buf_cells;
	struct stm32_pio_rx_dma rx_dma[STM32_PIO_NUM_CHANNELS];
	/* rx_max_edges capture times for each channel */
	uint32_t *rx_buf;
	size_t rx_max_edges;
};

struct stm32_pio_data {
	struct k_mutex lock;
	struct k_sem done;
	uint32_t tim_clk;
	/* Shared by all channels of the timer */
	uint32_t prescaler;
	uint32_t period;
	uint8_t reserved;
	uint8_t configured;
	bool idle_high[STM32_PIO_NUM_CHANNELS];
	enum pulse_io_dir dir[STM32_PIO_NUM_CHANNELS];
	uint32_t rx_idle_ticks[STM32_PIO_NUM_CHANNELS];
	/* Channels of the frame being sent */
	uint8_t busy;
	int dma_status;
	/* The timer is in use by a synchronous transmit or an RTIO submission */
	bool active;
	struct k_spinlock active_lock;
#ifdef CONFIG_RTIO
	struct mpsc io_q;
	/* Submission in progress, NULL when idle */
	struct rtio_iodev_sqe *cur;
	/* The transmit in progress completes an RTIO submission */
	bool rtio_tx;
	/* Receive in progress on rx_idx, with rx_edges edges at the last check */
	bool rx_active;
	uint8_t rx_idx;
	size_t rx_edges;
	struct k_timer rx_timer;
#endif
};

static int stm32_pio_channel_index(const struct device *dev, struct pulse_io_channel *chan)
{
	struct stm32_pio_data *data = dev->data;
	uintptr_t idx = (uintptr_t)chan - 1U;

	if ((idx >= STM32_PIO_NUM_CHANNELS) || ((data->reserved & BIT(idx)) == 0U)) {
		return -EINVAL;
	}

	return (int)idx;
}

static bool stm32_pio_channel_exists(TIM_TypeDef *timer, uint8_t idx)
{
	switch (idx) {
	case 0:
		return IS_TIM_CC1_INSTANCE(timer);
	case 1:
		return IS_TIM_CC2_INSTANCE(timer);
	case 2:
		return IS_TIM_CC3_INSTANCE(timer);
	case 3:
		return IS_TIM_CC4_INSTANCE(timer);
	default:
		return false;
	}
}

static volatile uint32_t *stm32_pio_ccr(TIM_TypeDef *timer, uint8_t idx)
{
	/* CCR1 to CCR4 are consecutive */
	return &timer->CCR1 + idx;
}

static uint32_t stm32_pio_max_reload(TIM_TypeDef *timer)
{
	return IS_TIM_32B_COUNTER_INSTANCE(timer) ? UINT32_MAX : UINT16_MAX;
}

static bool stm32_pio_claim(struct stm32_pio_data *data)
{
	k_spinlock_key_t key = k_spin_lock(&data->active_lock);
	bool claimed = !data->active;

	data->active = true;
	k_spin_unlock(&data->active_lock, key);

	return claimed;
}

static void stm32_pio_unclaim(struct stm32_pio_data *data)
{
	k_spinlock_key_t key = k_spin_lock(&data->active_lock);

	data->active = false;
	k_spin_unlock(&data->active_lock, key);
}

/* Park the channel at its idle level */
static void stm32_pio_set_idle(const struct device *dev, uint8_t idx)
{
	const struct stm32_pio_config *cfg = dev->config;
	struct stm32_pio_data *data = dev->data;

	LL_TIM_OC_SetMode(cfg->timer, ll_channels[idx],
			  data->idle_high[idx] ? LL_TIM_OCMODE_FORCED_ACTIVE
					       : LL_TIM_OCMODE_FORCED_INACTIVE);
}

/* Make the channel an output parked at its idle level */
static void stm32_pio_set_output(const struct device *dev, uint8_t idx)
{
	const struct stm32_pio_config *cfg = dev->config;

	/* The direction of a channel only changes while it is off */
	LL_TIM_CC_DisableChannel(cfg->timer, ll_channels[idx]);
	LL_TIM_OC_ConfigOutput(cfg->timer, ll_channels[idx],
			       LL_TIM_OCPOLARITY_HIGH | LL_TIM_OCIDLESTATE_LOW);
	LL_TIM_OC_EnablePreload(cfg->timer, ll_channels[idx]);
	stm32_pio_set_idle(dev, idx);
	LL_TIM_CC_EnableChannel(cfg->timer, ll_channels[idx]);
}

static void stm32_pio_halt(const struct device *dev, uint8_t channels)
{
	const struct stm32_pio_config *cfg = dev->config;

	LL_TIM_DisableDMAReq_UPDATE(cfg->timer);
	LL_TIM_DisableCounter(cfg->timer);
	for (uint8_t i = 0; i < STM32_PIO_NUM_CHANNELS; i++) {
		if ((channels & BIT(i)) != 0U) {
			stm32_pio_set_idle(dev, i);
		}
	}
}

/* Busy-wait for the last cells of a transmit, including the idle one, to shift out */
static void stm32_pio_wait_last_cells(const struct device *dev)
{
	struct stm32_pio_data *data = dev->data;
	uint32_t period_ns = (uint32_t)DIV_ROUND_UP((uint64_t)NSEC_PER_SEC *
							    (data->prescaler + 1U) * data->period,
						    data->tim_clk);

	k_busy_wait(DIV_ROUND_UP(3U * period_ns, NSEC_PER_USEC));
}

#ifdef CONFIG_RTIO
static void stm32_pio_rtio_complete(const struct device *dev, int result);
static void stm32_pio_rtio_tx_done(const struct device *dev, int status);
#endif

static void stm32_pio_dma_callback(const struct device *dma_dev, void *user_data,
				   uint32_t channel, int status)
{
	const struct device *dev = user_data;
	const struct stm32_pio_config *cfg = dev->config;
	struct stm32_pio_data *data = dev->data;

	ARG_UNUSED(dma_dev);
	ARG_UNUSED(channel);

	/* The last value is in the preload register: no more requests needed */
	LL_TIM_DisableDMAReq_UPDATE(cfg->timer);

#ifdef CONFIG_RTIO
	if (data->rtio_tx) {
		stm32_pio_rtio_tx_done(dev, status);
		return;
	}
#endif

	if (data->dma_status == 0) {
		data->dma_status = status;
	}
	k_sem_give(&data->done);
}

static int stm32_pio_get_capabilities(const struct device *dev, struct pulse_io_caps *caps)
{
	const struct stm32_pio_config *cfg = dev->config;
	struct stm32_pio_data *data = dev->data;
	uint32_t mask = 0U;
	uint32_t rx_mask = 0U;

	for (uint8_t i = 0; i < STM32_PIO_NUM_CHANNELS; i++) {
		if (stm32_pio_channel_exists(cfg->timer, i)) {
			mask |= BIT(i);
			if (STM32_PIO_RX && (cfg->rx_dma[i].dev != NULL)) {
				rx_mask |= BIT(i);
			}
		}
	}

	*caps = (struct pulse_io_caps){
		/* One cell of each frame is the trailing idle cell */
		.tx_max_streaming = cfg->buf_cells - 1U,
		.modes = PULSE_IO_MODE_CELL,
		.min_tick_ns = DIV_ROUND_UP(NSEC_PER_SEC, data->tim_clk),
		.max_tick_ns = (uint32_t)DIV_ROUND_UP((uint64_t)NSEC_PER_SEC * (UINT16_MAX + 1U),
						      data->tim_clk),
		.max_duration_ticks = stm32_pio_max_reload(cfg->timer),
		.rx_idle_threshold_max = (rx_mask != 0U) ? stm32_pio_max_reload(cfg->timer) : 0U,
		.tx_channel_mask = mask,
		.rx_channel_mask = rx_mask,
		.cell_duty_bits = IS_TIM_32B_COUNTER_INSTANCE(cfg->timer) ? 32U : 16U,
		.num_channels = STM32_PIO_NUM_CHANNELS,
		.supports_tx = true,
		.supports_rx = rx_mask != 0U,
		.tx_sync_multi_channel = true,
	};

	return 0;
}

static int stm32_pio_channel_get(const struct device *dev, uint8_t channel_idx,
				 struct pulse_io_channel **chan)
{
	const struct stm32_pio_config *cfg = dev->config;
	struct stm32_pio_data *data = dev->data;
	int ret = 0;

	if ((channel_idx >= STM32_PIO_NUM_CHANNELS) ||
	    !stm32_pio_channel_exists(cfg->timer, channel_idx)) {
		return -ENODEV;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if ((data->reserved & BIT(channel_idx)) != 0U) {
		ret = -EBUSY;
	} else {
		data->reserved |= BIT(channel_idx);
		*chan = (struct pulse_io_channel *)(uintptr_t)(channel_idx + 1U);
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

static int stm32_pio_channel_release(const struct device *dev, struct pulse_io_channel *chan)
{
	struct stm32_pio_data *data = dev->data;
	int idx = stm32_pio_channel_index(dev, chan);

	if (idx < 0) {
		return idx;
	}

	/* Taking the lock waits for a synchronous transfer on this channel to end */
	k_mutex_lock(&data->lock, K_FOREVER);
	stm32_pio_set_idle(dev, idx);
	data->reserved &= ~BIT(idx);
	data->configured &= ~BIT(idx);
	k_mutex_unlock(&data->lock);

	return 0;
}

static int stm32_pio_channel_configure(const struct device *dev, struct pulse_io_channel *chan,
				       const struct pulse_io_config *pio_cfg)
{
	const struct stm32_pio_config *cfg = dev->config;
	struct stm32_pio_data *data = dev->data;
	int idx = stm32_pio_channel_index(dev, chan);
	uint32_t prescaler;
	int ret = 0;

	if (idx < 0) {
		return idx;
	}

	if ((pio_cfg->mode != PULSE_IO_MODE_CELL) || pio_cfg->carrier_en ||
	    (pio_cfg->dir == PULSE_IO_DIR_RX)) {
		return -ENOTSUP;
	}

	/* A receive needs the capture DMA of the channel, and RTIO to submit it */
	if ((pio_cfg->dir == PULSE_IO_DIR_TX_RX) &&
	    (!STM32_PIO_RX || (cfg->rx_dma[idx].dev == NULL))) {
		return -ENOTSUP;
	}

	if ((pio_cfg->resolution_hz == 0U) || (pio_cfg->resolution_hz > data->tim_clk) ||
	    (pio_cfg->cell_period_ticks == 0U) ||
	    (pio_cfg->cell_period_ticks - 1U > stm32_pio_max_reload(cfg->timer)) ||
	    ((pio_cfg->dir == PULSE_IO_DIR_TX_RX) &&
	     ((pio_cfg->rx_idle_threshold_ticks == 0U) ||
	      (pio_cfg->rx_idle_threshold_ticks > stm32_pio_max_reload(cfg->timer))))) {
		return -EINVAL;
	}

	prescaler = DIV_ROUND_CLOSEST(data->tim_clk, pio_cfg->resolution_hz) - 1U;
	if (prescaler > UINT16_MAX) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	/* The channels share the timer counter, hence its timing */
	if (((data->configured & ~BIT(idx)) != 0U) &&
	    ((prescaler != data->prescaler) || (pio_cfg->cell_period_ticks != data->period))) {
		ret = -EBUSY;
		goto unlock;
	}

	data->prescaler = prescaler;
	data->period = pio_cfg->cell_period_ticks;
	data->idle_high[idx] = pio_cfg->idle_high;
	data->dir[idx] = pio_cfg->dir;
	data->rx_idle_ticks[idx] = pio_cfg->rx_idle_threshold_ticks;
	data->configured |= BIT(idx);

	stm32_pio_set_output(dev, idx);

unlock:
	k_mutex_unlock(&data->lock);

	return ret;
}

/*
 * Load the requests of the channels in idx and start the counter: the
 * channels start on the same update event. Returns the number of cells,
 * the idle one included, of which the DMA sends those after the second.
 */
static int stm32_pio_tx_start(const struct device *dev, const uint8_t *idx,
			      const struct pulse_io_tx_req *reqs, size_t num)
{
	const struct stm32_pio_config *cfg = dev->config;
	struct stm32_pio_data *data = dev->data;
	TIM_TypeDef *timer = cfg->timer;
	struct dma_block_config block = {0};
	struct dma_config dma_cfg = {0};
	uint32_t ccr[STM32_PIO_NUM_CHANNELS];
	uint8_t channels = 0U;
	uint8_t first = STM32_PIO_NUM_CHANNELS;
	uint8_t last = 0U;
	size_t count = 0U;
	size_t span;
	unsigned int key;
	int ret;

	for (size_t i = 0; i < num; i++) {
		channels |= BIT(idx[i]);
		first = MIN(first, idx[i]);
		last = MAX(last, idx[i]);
		count = MAX(count, reqs[i].count);
	}

	/*
	 * The DMA burst writes the compare registers from the first to the last
	 * channel: the channels in between that don't transmit keep their value.
	 * The idle level is a compare value of 0, in either PWM mode.
	 */
	span = last - first + 1U;
	for (uint8_t ch = first; ch <= last; ch++) {
		ccr[ch] = ((channels & BIT(ch)) == 0U) ? *stm32_pio_ccr(timer, ch) : 0U;
	}

	/* One more cell at the idle level ends the frame */
	count++;
	for (size_t c = 0; c < count; c++) {
		uint32_t *cell = &cfg->buf[c * span];

		for (uint8_t ch = first; ch <= last; ch++) {
			cell[ch - first] = ccr[ch];
		}
		for (size_t i = 0; i < num; i++) {
			if (c < reqs[i].count) {
				uint32_t duty = MIN(reqs[i].cells[c].duty, data->period);

				/* PWM mode 2 holds the line low for the compare value */
				cell[idx[i] - first] =
					data->idle_high[idx[i]] ? data->period - duty : duty;
			}
		}
	}

	sys_cache_data_flush_range(cfg->buf, count * span * sizeof(cfg->buf[0]));

	LL_TIM_DisableCounter(timer);
	LL_TIM_DisableDMAReq_UPDATE(timer);
	LL_TIM_SetPrescaler(timer, data->prescaler);
	LL_TIM_SetAutoReload(timer, data->period - 1U);
	LL_TIM_EnableARRPreload(timer);
	LL_TIM_SetCounter(timer, 0U);

	/* The update event makes the first duty cycles active, then preload the second ones */
	for (uint8_t ch = first; ch <= last; ch++) {
		*stm32_pio_ccr(timer, ch) = cfg->buf[ch - first];
	}
	LL_TIM_GenerateEvent_UPDATE(timer);
	for (uint8_t ch = first; ch <= last; ch++) {
		*stm32_pio_ccr(timer, ch) = cfg->buf[span + ch - first];
	}

	if (count > 2U) {
		/* CCR1 to CCR4 are consecutive, and DBA counts words from CR1 */
		timer->DCR = (((offsetof(TIM_TypeDef, CCR1) / sizeof(uint32_t)) + first)
			      << TIM_DCR_DBA_Pos) |
			     ((span - 1U) << TIM_DCR_DBL_Pos);

		block.source_address = (uint32_t)(uintptr_t)&cfg->buf[2U * span];
		block.dest_address = (uint32_t)(uintptr_t)&timer->DMAR;
		block.block_size = (count - 2U) * span * sizeof(cfg->buf[0]);
		block.source_addr_adj = DMA_ADDR_ADJ_INCREMENT;
		block.dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;

		dma_cfg.dma_slot = cfg->dma_slot;
		dma_cfg.channel_direction = MEMORY_TO_PERIPHERAL;
		dma_cfg.channel_priority = cfg->dma_priority;
		dma_cfg.source_data_size = sizeof(cfg->buf[0]);
		dma_cfg.dest_data_size = sizeof(cfg->buf[0]);
		/* Single transfers: the timer requests one per register of the burst */
		dma_cfg.source_burst_length = sizeof(cfg->buf[0]);
		dma_cfg.dest_burst_length = sizeof(cfg->buf[0]);
		dma_cfg.block_count = 1U;
		dma_cfg.head_block = &block;
		dma_cfg.dma_callback = stm32_pio_dma_callback;
		dma_cfg.user_data = (void *)dev;

		ret = dma_config(cfg->dma_dev, cfg->dma_channel, &dma_cfg);
		if (ret < 0) {
			LOG_ERR("DMA configuration failed (%d)", ret);
			return ret;
		}

		k_sem_reset(&data->done);
		data->dma_status = 0;

		ret = dma_start(cfg->dma_dev, cfg->dma_channel);
		if (ret < 0) {
			LOG_ERR("DMA start failed (%d)", ret);
			return ret;
		}

		LL_TIM_EnableDMAReq_UPDATE(timer);
	}

	data->busy = channels;

	/*
	 * The first period starts as soon as the counter does. A channel leaving a
	 * forced level for a PWM mode keeps that level until the comparison result
	 * changes, which would swallow the first cell: the PWM mode only takes
	 * effect at once when entered from the frozen mode, which holds the level.
	 */
	key = irq_lock();
	for (size_t i = 0; i < num; i++) {
		LL_TIM_OC_SetMode(timer, ll_channels[idx[i]], LL_TIM_OCMODE_FROZEN);
		LL_TIM_OC_SetMode(timer, ll_channels[idx[i]],
				  data->idle_high[idx[i]] ? LL_TIM_OCMODE_PWM2
							  : LL_TIM_OCMODE_PWM1);
	}
	LL_TIM_EnableCounter(timer);
	irq_unlock(key);

	return (int)count;
}

/* Stop the transmit once its last cells have shifted out */
static void stm32_pio_tx_end(const struct device *dev, bool dma)
{
	const struct stm32_pio_config *cfg = dev->config;
	struct stm32_pio_data *data = dev->data;

	stm32_pio_wait_last_cells(dev);
	if (dma) {
		(void)dma_stop(cfg->dma_dev, cfg->dma_channel);
	}
	stm32_pio_halt(dev, data->busy);
	data->busy = 0U;
}

/*
 * Transmit the requests of the channels in idx, which start on the same
 * update event. A channel that has sent all its cells stays at its idle level.
 */
static int stm32_pio_transmit(const struct device *dev, const uint8_t *idx,
			      const struct pulse_io_tx_req *reqs, size_t num, k_timeout_t timeout)
{
	const struct stm32_pio_config *cfg = dev->config;
	struct stm32_pio_data *data = dev->data;
	uint8_t channels = 0U;
	size_t count = 0U;
	int ret;

	for (size_t i = 0; i < num; i++) {
		if ((channels & BIT(idx[i])) != 0U) {
			return -EINVAL;
		}
		if (reqs[i].loop_count != 0U) {
			return -ENOTSUP;
		}
		if (reqs[i].count + 1U > cfg->buf_cells) {
			return -ENOMEM;
		}
		channels |= BIT(idx[i]);
		count = MAX(count, reqs[i].count);
	}

	if (count == 0U) {
		return 0;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if ((channels & ~data->configured) != 0U) {
		ret = -EINVAL;
		goto unlock;
	}

	/* The timer may be in use by RTIO submissions */
	if (!stm32_pio_claim(data)) {
		ret = -EBUSY;
		goto unlock;
	}

	ret = stm32_pio_tx_start(dev, idx, reqs, num);
	if (ret < 0) {
		goto unclaim;
	}

	count = ret;
	ret = 0;

	if (count > 2U) {
		ret = k_sem_take(&data->done, timeout);
		if (ret < 0) {
			(void)dma_stop(cfg->dma_dev, cfg->dma_channel);
			stm32_pio_halt(dev, data->busy);
			data->busy = 0U;
			ret = -EAGAIN;
			goto unclaim;
		}

		if (data->dma_status < 0) {
			(void)dma_stop(cfg->dma_dev, cfg->dma_channel);
			stm32_pio_halt(dev, data->busy);
			data->busy = 0U;
			ret = (data->dma_status == -ECANCELED) ? -ECANCELED : -EIO;
			goto unclaim;
		}
	}

	stm32_pio_tx_end(dev, count > 2U);

unclaim:
	stm32_pio_unclaim(data);
unlock:
	k_mutex_unlock(&data->lock);

	return ret;
}

static int stm32_pio_transmit_sync(const struct device *dev, struct pulse_io_channel *chan,
				   const struct pulse_io_tx_req *req, k_timeout_t timeout)
{
	int idx = stm32_pio_channel_index(dev, chan);
	uint8_t ch;

	if (idx < 0) {
		return idx;
	}

	ch = (uint8_t)idx;

	return stm32_pio_transmit(dev, &ch, req, 1U, timeout);
}

static int stm32_pio_transmit_multi_sync(const struct device *dev,
					 struct pulse_io_channel *const *chans,
					 const struct pulse_io_tx_req *reqs, size_t num,
					 k_timeout_t timeout)
{
	uint8_t idx[STM32_PIO_NUM_CHANNELS];

	/* A repeated channel would exceed the channels of the timer */
	if (num > STM32_PIO_NUM_CHANNELS) {
		return -EINVAL;
	}

	for (size_t i = 0; i < num; i++) {
		int ret = stm32_pio_channel_index(dev, chans[i]);

		if (ret < 0) {
			return ret;
		}
		idx[i] = (uint8_t)ret;
	}

	return stm32_pio_transmit(dev, idx, reqs, num, timeout);
}

static int stm32_pio_receive_sync(const struct device *dev, struct pulse_io_channel *chan,
				  const struct pulse_io_rx_req *req, size_t *count,
				  k_timeout_t timeout)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(chan);
	ARG_UNUSED(req);
	ARG_UNUSED(count);
	ARG_UNUSED(timeout);

	/* A receive follows a transmit: it is submitted with RTIO */
	return -ENOTSUP;
}

static int stm32_pio_stop(const struct device *dev, struct pulse_io_channel *chan)
{
	struct stm32_pio_data *data = dev->data;
	int idx = stm32_pio_channel_index(dev, chan);
	unsigned int key;

	if (idx < 0) {
		return idx;
	}

	/* Wake up the transmitting thread, which stops the DMA and the timer */
	key = irq_lock();
	if ((data->busy & BIT(idx)) != 0U) {
		data->dma_status = -ECANCELED;
		k_sem_give(&data->done);
	}
	irq_unlock(key);

	return 0;
}

#ifdef CONFIG_RTIO

static void stm32_pio_rx_set_dma_req(TIM_TypeDef *timer, uint8_t idx, bool enable)
{
	static const uint32_t cc_dma[STM32_PIO_NUM_CHANNELS] = {
		TIM_DIER_CC1DE,
		TIM_DIER_CC2DE,
		TIM_DIER_CC3DE,
		TIM_DIER_CC4DE,
	};

	if (enable) {
		SET_BIT(timer->DIER, cc_dma[idx]);
	} else {
		CLEAR_BIT(timer->DIER, cc_dma[idx]);
	}
}

/* End the receive: write the captured edges as symbols to the RTIO buffer */
static void stm32_pio_rx_end(const struct device *dev)
{
	const struct stm32_pio_config *cfg = dev->config;
	struct stm32_pio_data *data = dev->data;
	TIM_TypeDef *timer = cfg->timer;
	uint8_t idx = data->rx_idx;
	const struct stm32_pio_rx_dma *rx_dma = &cfg->rx_dma[idx];
	const uint32_t *times = &cfg->rx_buf[idx * cfg->rx_max_edges];
	uint32_t mask = stm32_pio_max_reload(timer);
	struct dma_status status = {0};
	struct pulse_symbol *out;
	uint32_t out_len = 0U;
	size_t edges = cfg->rx_max_edges;
	uint32_t now;
	uint32_t prev = 0U;
	bool level;
	size_t num;
	int ret;

	k_spinlock_key_t key = k_spin_lock(&data->active_lock);

	if (!data->rx_active) {
		k_spin_unlock(&data->active_lock, key);
		return;
	}
	data->rx_active = false;
	k_spin_unlock(&data->active_lock, key);

	k_timer_stop(&data->rx_timer);
	now = LL_TIM_GetCounter(timer);
	if (dma_get_status(rx_dma->dev, rx_dma->channel, &status) == 0) {
		edges = cfg->rx_max_edges - (status.pending_length / sizeof(times[0]));
	}
	(void)dma_stop(rx_dma->dev, rx_dma->channel);
	stm32_pio_rx_set_dma_req(timer, idx, false);
	LL_TIM_DisableCounter(timer);
	stm32_pio_set_output(dev, idx);

	sys_cache_data_invd_range((void *)times, cfg->rx_max_edges * sizeof(times[0]));

	/* One symbol before each edge, and the one after the last edge */
	ret = rtio_sqe_rx_buf(data->cur, sizeof(*out), (edges + 1U) * sizeof(*out),
			      (uint8_t **)&out, &out_len);
	if (ret < 0) {
		stm32_pio_unclaim(data);
		stm32_pio_rtio_complete(dev, ret);
		return;
	}

	num = MIN(edges + 1U, out_len / sizeof(*out));
	level = data->idle_high[idx];
	for (size_t i = 0; i < num; i++) {
		uint32_t t = (i < edges) ? times[i] : now;

		out[i] = (struct pulse_symbol){.duration = (t - prev) & mask, .level = level};
		level = !level;
		prev = t;
	}

	stm32_pio_unclaim(data);
	stm32_pio_rtio_complete(dev, (int)(num * sizeof(*out)));
}

/* No edge for the idle threshold, after the first or the last one: the receive ends */
static void stm32_pio_rx_timer(struct k_timer *timer)
{
	const struct device *dev = k_timer_user_data_get(timer);
	const struct stm32_pio_config *cfg = dev->config;
	struct stm32_pio_data *data = dev->data;
	const struct stm32_pio_rx_dma *rx_dma = &cfg->rx_dma[data->rx_idx];
	struct dma_status status = {0};
	size_t edges;

	if (dma_get_status(rx_dma->dev, rx_dma->channel, &status) < 0) {
		stm32_pio_rx_end(dev);
		return;
	}

	edges = cfg->rx_max_edges - (status.pending_length / sizeof(cfg->rx_buf[0]));
	if (edges > data->rx_edges) {
		data->rx_edges = edges;
		return;
	}

	stm32_pio_rx_end(dev);
}

static void stm32_pio_rx_dma_callback(const struct device *dma_dev, void *user_data,
				      uint32_t channel, int status)
{
	ARG_UNUSED(dma_dev);
	ARG_UNUSED(channel);
	ARG_UNUSED(status);

	/* The buffer is full */
	stm32_pio_rx_end(user_data);
}

static int stm32_pio_rx_start(const struct device *dev, uint8_t idx)
{
	const struct stm32_pio_config *cfg = dev->config;
	struct stm32_pio_data *data = dev->data;
	TIM_TypeDef *timer = cfg->timer;
	const struct stm32_pio_rx_dma *rx_dma = &cfg->rx_dma[idx];
	uint32_t *times = &cfg->rx_buf[idx * cfg->rx_max_edges];
	struct dma_block_config block = {0};
	struct dma_config dma_cfg = {0};
	uint32_t idle_us;
	int ret;

	if (data->dir[idx] != PULSE_IO_DIR_TX_RX) {
		return -ENOTSUP;
	}

	/* A free-running counter at the resolution of the channel times the edges */
	LL_TIM_DisableCounter(timer);
	LL_TIM_DisableDMAReq_UPDATE(timer);
	LL_TIM_SetPrescaler(timer, data->prescaler);
	LL_TIM_SetAutoReload(timer, stm32_pio_max_reload(timer));
	LL_TIM_SetCounter(timer, 0U);
	LL_TIM_GenerateEvent_UPDATE(timer);

	/* Input capture on both edges: the direction of a channel only changes while it is off */
	LL_TIM_CC_DisableChannel(timer, ll_channels[idx]);
	LL_TIM_IC_SetActiveInput(timer, ll_channels[idx], LL_TIM_ACTIVEINPUT_DIRECTTI);
	LL_TIM_IC_SetPrescaler(timer, ll_channels[idx], LL_TIM_ICPSC_DIV1);
	LL_TIM_IC_SetFilter(timer, ll_channels[idx], LL_TIM_IC_FILTER_FDIV1);
#if STM32_PIO_RX
	LL_TIM_IC_SetPolarity(timer, ll_channels[idx], LL_TIM_IC_POLARITY_BOTHEDGE);
#endif

	sys_cache_data_flush_and_invd_range(times, cfg->rx_max_edges * sizeof(times[0]));

	block.source_address = (uint32_t)(uintptr_t)stm32_pio_ccr(timer, idx);
	block.dest_address = (uint32_t)(uintptr_t)times;
	block.block_size = cfg->rx_max_edges * sizeof(times[0]);
	block.source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
	block.dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;

	dma_cfg.dma_slot = rx_dma->slot;
	dma_cfg.channel_direction = PERIPHERAL_TO_MEMORY;
	dma_cfg.channel_priority = rx_dma->priority;
	dma_cfg.source_data_size = sizeof(times[0]);
	dma_cfg.dest_data_size = sizeof(times[0]);
	dma_cfg.source_burst_length = sizeof(times[0]);
	dma_cfg.dest_burst_length = sizeof(times[0]);
	dma_cfg.block_count = 1U;
	dma_cfg.head_block = &block;
	dma_cfg.dma_callback = stm32_pio_rx_dma_callback;
	dma_cfg.user_data = (void *)dev;

	ret = dma_config(rx_dma->dev, rx_dma->channel, &dma_cfg);
	if (ret < 0) {
		LOG_ERR("RX DMA configuration failed (%d)", ret);
		stm32_pio_set_output(dev, idx);
		return ret;
	}

	ret = dma_start(rx_dma->dev, rx_dma->channel);
	if (ret < 0) {
		LOG_ERR("RX DMA start failed (%d)", ret);
		stm32_pio_set_output(dev, idx);
		return ret;
	}

	data->rx_idx = idx;
	data->rx_edges = 0U;
	data->rx_active = true;

	stm32_pio_rx_set_dma_req(timer, idx, true);
	LL_TIM_CC_EnableChannel(timer, ll_channels[idx]);
	LL_TIM_EnableCounter(timer);

	idle_us = (uint32_t)DIV_ROUND_UP((uint64_t)USEC_PER_SEC * (data->prescaler + 1U) *
						 data->rx_idle_ticks[idx],
					 data->tim_clk);
	k_timer_start(&data->rx_timer, K_USEC(idle_us), K_USEC(idle_us));

	return 0;
}

static void stm32_pio_rtio_next(const struct device *dev);

static void stm32_pio_rtio_complete(const struct device *dev, int result)
{
	struct stm32_pio_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->active_lock);
	struct rtio_iodev_sqe *sqe = data->cur;

	data->cur = NULL;
	k_spin_unlock(&data->active_lock, key);

	/* Completing a submission may submit the one chained to it */
	if (result < 0) {
		rtio_iodev_sqe_err(sqe, result);
	} else {
		rtio_iodev_sqe_ok(sqe, result);
	}

	stm32_pio_rtio_next(dev);
}

static void stm32_pio_rtio_tx_done(const struct device *dev, int status)
{
	struct stm32_pio_data *data = dev->data;

	stm32_pio_tx_end(dev, true);
	data->rtio_tx = false;
	stm32_pio_unclaim(data);
	stm32_pio_rtio_complete(dev, (status < 0) ? -EIO : 0);
}

static int stm32_pio_rtio_start(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe)
{
	const struct stm32_pio_config *cfg = dev->config;
	struct stm32_pio_data *data = dev->data;
	struct rtio_sqe *sqe = &iodev_sqe->sqe;
	int idx = stm32_pio_channel_index(dev, pulse_io_iodev_channel(iodev_sqe));
	uint8_t ch;
	int ret;

	if (idx < 0) {
		return idx;
	}

	ch = (uint8_t)idx;

	if ((data->configured & BIT(ch)) == 0U) {
		return -EINVAL;
	}

	switch (sqe->op) {
	case RTIO_OP_TX: {
		const struct pulse_io_tx_req req = {
			.cells = (const struct pulse_cell *)sqe->tx.buf,
			.count = sqe->tx.buf_len / sizeof(struct pulse_cell),
		};

		if ((req.count == 0U) || (req.count + 1U > cfg->buf_cells)) {
			return (req.count == 0U) ? -EINVAL : -ENOMEM;
		}

		if (!stm32_pio_claim(data)) {
			return -EBUSY;
		}

		data->rtio_tx = true;
		ret = stm32_pio_tx_start(dev, &ch, &req, 1U);
		if (ret < 0) {
			data->rtio_tx = false;
			stm32_pio_unclaim(data);
			return ret;
		}

		/* Without DMA, the cells are already loaded: wait for them here */
		if (ret <= 2) {
			stm32_pio_tx_end(dev, false);
			data->rtio_tx = false;
			stm32_pio_unclaim(data);
			stm32_pio_rtio_complete(dev, 0);
		}

		return 0;
	}

	case RTIO_OP_RX:
		if (!STM32_PIO_RX) {
			return -ENOTSUP;
		}

		if (!stm32_pio_claim(data)) {
			return -EBUSY;
		}

		ret = stm32_pio_rx_start(dev, ch);
		if (ret < 0) {
			stm32_pio_unclaim(data);
		}

		return ret;

	default:
		return -ENOTSUP;
	}
}

static void stm32_pio_rtio_next(const struct device *dev)
{
	struct stm32_pio_data *data = dev->data;
	struct mpsc_node *node;
	k_spinlock_key_t key = k_spin_lock(&data->active_lock);

	if (data->cur != NULL) {
		k_spin_unlock(&data->active_lock, key);
		return;
	}

	node = mpsc_pop(&data->io_q);
	if (node == NULL) {
		k_spin_unlock(&data->active_lock, key);
		return;
	}

	data->cur = CONTAINER_OF(node, struct rtio_iodev_sqe, q);
	k_spin_unlock(&data->active_lock, key);

	int ret = stm32_pio_rtio_start(dev, data->cur);

	if (ret < 0) {
		stm32_pio_rtio_complete(dev, ret);
	}
}

static void stm32_pio_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe)
{
	struct stm32_pio_data *data = dev->data;

	mpsc_push(&data->io_q, &iodev_sqe->q);
	stm32_pio_rtio_next(dev);
}

#endif /* CONFIG_RTIO */

static DEVICE_API(pulse_io, stm32_pio_api) = {
	.get_capabilities = stm32_pio_get_capabilities,
	.channel_get = stm32_pio_channel_get,
	.channel_release = stm32_pio_channel_release,
	.channel_configure = stm32_pio_channel_configure,
	.transmit_sync = stm32_pio_transmit_sync,
	.receive_sync = stm32_pio_receive_sync,
	.stop = stm32_pio_stop,
	.transmit_multi_sync = stm32_pio_transmit_multi_sync,
#ifdef CONFIG_RTIO
	.submit = stm32_pio_submit,
#endif
};

static int stm32_pio_init(const struct device *dev)
{
	const struct stm32_pio_config *cfg = dev->config;
	struct stm32_pio_data *data = dev->data;
	const struct device *clk = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
	int ret;

	k_mutex_init(&data->lock);
	k_sem_init(&data->done, 0, 1);

#ifdef CONFIG_RTIO
	mpsc_init(&data->io_q);
	k_timer_init(&data->rx_timer, stm32_pio_rx_timer, NULL);
	k_timer_user_data_set(&data->rx_timer, (void *)dev);
#endif

	if (!device_is_ready(cfg->dma_dev)) {
		LOG_ERR_DEVICE_NOT_READY(cfg->dma_dev);
		return -ENODEV;
	}

	for (uint8_t i = 0; i < STM32_PIO_NUM_CHANNELS; i++) {
		if ((cfg->rx_dma[i].dev != NULL) && !device_is_ready(cfg->rx_dma[i].dev)) {
			LOG_ERR_DEVICE_NOT_READY(cfg->rx_dma[i].dev);
			return -ENODEV;
		}
	}

	ret = clock_control_on(clk, (clock_control_subsys_t)&cfg->pclken[0]);
	if (ret < 0) {
		LOG_ERR("Could not enable the timer clock (%d)", ret);
		return ret;
	}

	if (cfg->pclk_len < 2U) {
		LOG_ERR("Timer clock source is not specified");
		return -EINVAL;
	}

	ret = clock_control_get_rate(clk, (clock_control_subsys_t)&cfg->pclken[1], &data->tim_clk);
	if (ret < 0) {
		LOG_ERR("Timer clock rate get error (%d)", ret);
		return ret;
	}

	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("pinctrl setup failed (%d)", ret);
		return ret;
	}

	if (IS_TIM_COUNTER_MODE_SELECT_INSTANCE(cfg->timer)) {
		LL_TIM_SetCounterMode(cfg->timer, LL_TIM_COUNTERMODE_UP);
	}

	if (IS_TIM_BREAK_INSTANCE(cfg->timer)) {
		LL_TIM_EnableAllOutputs(cfg->timer);
	}

	return 0;
}

#define STM32_PIO_TIMER(inst) DT_INST_PARENT(inst)

#define STM32_PIO_RX_DMA(inst, name)                                                               \
	COND_CODE_1(DT_INST_DMAS_HAS_NAME(inst, name),                                             \
		    ({                                                                             \
			    .dev = DEVICE_DT_GET(STM32_DT_INST_DMA_CTLR(inst, name)),              \
			    .channel = DT_INST_DMAS_CELL_BY_NAME(inst, name, channel),             \
			    .slot = STM32_DT_INST_DMA_SLOT(inst, name),                            \
			    .priority = STM32_DMA_CONFIG_PRIORITY(                                 \
				    STM32_DT_INST_DMA_CHANNEL_CONFIG(inst, name)),                 \
		    }),                                                                            \
		    ({0}))

#define STM32_PIO_HAS_RX(inst)                                                                     \
	(DT_INST_DMAS_HAS_NAME(inst, ch1) || DT_INST_DMAS_HAS_NAME(inst, ch2) ||                   \
	 DT_INST_DMAS_HAS_NAME(inst, ch3) || DT_INST_DMAS_HAS_NAME(inst, ch4))

#define STM32_PIO_RX_EDGES(inst) (STM32_PIO_HAS_RX(inst) ? DT_INST_PROP(inst, st_max_edges) : 1)

#define STM32_PIO_INIT(inst)                                                                       \
	BUILD_ASSERT(DT_INST_PROP(inst, st_max_cells) >= 2,                                        \
		     "st,max-cells must leave room for a data and an idle cell");                 \
	BUILD_ASSERT(!STM32_PIO_HAS_RX(inst) ||                                                    \
			     ((DT_INST_PROP(inst, st_max_edges) * sizeof(uint32_t)) %              \
			      STM32_PIO_BUF_ALIGN) == 0,                                           \
		     "st,max-edges must fill whole data cache lines");                            \
                                                                                                   \
	PINCTRL_DT_INST_DEFINE(inst);                                                              \
                                                                                                   \
	static const struct stm32_pclken stm32_pio_pclken_##inst[] =                               \
		STM32_DT_CLOCKS(STM32_PIO_TIMER(inst));                                            \
                                                                                                   \
	static uint32_t stm32_pio_buf_##inst[DT_INST_PROP(inst, st_max_cells) *                    \
					     STM32_PIO_NUM_CHANNELS]                               \
		__aligned(STM32_PIO_BUF_ALIGN);                                                \
                                                                                                   \
	static uint32_t stm32_pio_rx_buf_##inst[STM32_PIO_RX_EDGES(inst) *                         \
						STM32_PIO_NUM_CHANNELS]                            \
		__aligned(STM32_PIO_BUF_ALIGN);                                                \
                                                                                                   \
	static const struct stm32_pio_config stm32_pio_config_##inst = {                           \
		.timer = (TIM_TypeDef *)DT_REG_ADDR(STM32_PIO_TIMER(inst)),                        \
		.pclken = stm32_pio_pclken_##inst,                                                 \
		.pclk_len = DT_NUM_CLOCKS(STM32_PIO_TIMER(inst)),                                  \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                      \
		.dma_dev = DEVICE_DT_GET(STM32_DT_INST_DMA_CTLR(inst, tx)),                        \
		.dma_channel = DT_INST_DMAS_CELL_BY_NAME(inst, tx, channel),                       \
		.dma_slot = STM32_DT_INST_DMA_SLOT(inst, tx),                                      \
		.dma_priority = STM32_DMA_CONFIG_PRIORITY(                                         \
			STM32_DT_INST_DMA_CHANNEL_CONFIG(inst, tx)),                               \
		.buf = stm32_pio_buf_##inst,                                                       \
		.buf_cells = DT_INST_PROP(inst, st_max_cells),                                     \
		.rx_dma = {STM32_PIO_RX_DMA(inst, ch1), STM32_PIO_RX_DMA(inst, ch2),               \
			   STM32_PIO_RX_DMA(inst, ch3), STM32_PIO_RX_DMA(inst, ch4)},              \
		.rx_buf = stm32_pio_rx_buf_##inst,                                                 \
		.rx_max_edges = STM32_PIO_RX_EDGES(inst),                                          \
	};                                                                                         \
                                                                                                   \
	static struct stm32_pio_data stm32_pio_data_##inst;                                        \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, stm32_pio_init, NULL, &stm32_pio_data_##inst,                  \
			      &stm32_pio_config_##inst, POST_KERNEL,                               \
			      CONFIG_PULSE_IO_INIT_PRIORITY, &stm32_pio_api);

DT_INST_FOREACH_STATUS_OKAY(STM32_PIO_INIT)
