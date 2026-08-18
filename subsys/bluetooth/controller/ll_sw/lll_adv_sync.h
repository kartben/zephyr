/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_BT_CONTROLLER_LL_SW_LLL_ADV_SYNC_H_
#define ZEPHYR_BT_CONTROLLER_LL_SW_LLL_ADV_SYNC_H_

int lll_adv_sync_init(void);
int lll_adv_sync_reset(void);
void lll_adv_sync_prepare(void *param);

extern uint16_t ull_adv_sync_lll_handle_get(const struct lll_adv_sync *lll);

extern void ull_adv_sync_lll_syncinfo_fill(struct pdu_adv *pdu, struct lll_adv_aux *lll_aux);

#endif /* ZEPHYR_BT_CONTROLLER_LL_SW_LLL_ADV_SYNC_H_ */
