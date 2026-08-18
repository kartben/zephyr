/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_BT_CONTROLLER_LL_SW_LLL_ADV_ISO_H_
#define ZEPHYR_BT_CONTROLLER_LL_SW_LLL_ADV_ISO_H_

int lll_adv_iso_init(void);
int lll_adv_iso_reset(void);
void lll_adv_iso_create_prepare(void *param);
void lll_adv_iso_prepare(void *param);

extern struct lll_adv_iso_stream *ull_adv_iso_lll_stream_get(uint16_t handle);

extern void ull_adv_iso_lll_biginfo_fill(struct pdu_adv *pdu, struct lll_adv_sync *lll_sync);

#endif /* ZEPHYR_BT_CONTROLLER_LL_SW_LLL_ADV_ISO_H_ */
