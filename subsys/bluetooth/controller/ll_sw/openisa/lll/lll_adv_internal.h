/*
 * Copyright (c) 2018-2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_BT_CONTROLLER_LL_SW_OPENISA_LLL_LLL_ADV_INTERNAL_H_
#define ZEPHYR_BT_CONTROLLER_LL_SW_OPENISA_LLL_LLL_ADV_INTERNAL_H_

struct pdu_adv *lll_adv_pdu_latest_get(struct lll_adv_pdu *pdu,
				       uint8_t *is_modified);

static inline struct pdu_adv *lll_adv_data_latest_get(struct lll_adv *lll,
						      uint8_t *is_modified)
{
	return lll_adv_pdu_latest_get(&lll->adv_data, is_modified);
}

static inline struct pdu_adv *lll_adv_scan_rsp_latest_get(struct lll_adv *lll,
							  uint8_t *is_modified)
{
	return lll_adv_pdu_latest_get(&lll->scan_rsp, is_modified);
}

static inline struct pdu_adv *lll_adv_data_curr_get(struct lll_adv *lll)
{
	return (void *)lll->adv_data.pdu[lll->adv_data.first];
}

static inline struct pdu_adv *lll_adv_scan_rsp_curr_get(struct lll_adv *lll)
{
	return (void *)lll->scan_rsp.pdu[lll->scan_rsp.first];
}

#endif /* ZEPHYR_BT_CONTROLLER_LL_SW_OPENISA_LLL_LLL_ADV_INTERNAL_H_ */
