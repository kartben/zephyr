/*
 * Copyright (c) 2018-2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_BT_CONTROLLER_LL_SW_OPENISA_LLL_LLL_INTERNAL_H_
#define ZEPHYR_BT_CONTROLLER_LL_SW_OPENISA_LLL_LLL_INTERNAL_H_

int lll_prepare_done(void *param);
int lll_done(void *param);
bool lll_is_done(void *param);
int lll_is_abort_cb(void *next, void *curr, lll_prepare_cb_t *resume_cb);
void lll_abort_cb(struct lll_prepare_param *prepare_param, void *param);

int lll_clk_on(void);
int lll_clk_on_wait(void);
int lll_clk_off(void);
uint32_t lll_event_offset_get(struct ull_hdr *ull);
uint32_t lll_preempt_calc(struct ull_hdr *ull, uint8_t ticker_id,
			  uint32_t ticks_at_event);
void lll_chan_set(uint32_t chan);
void lll_isr_status_reset(void);

#endif /* ZEPHYR_BT_CONTROLLER_LL_SW_OPENISA_LLL_LLL_INTERNAL_H_ */
