/*
 * Copyright (c) 2021 Demant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Helper functions to initialize and reset ull_peripheral_iso module */

#ifndef ZEPHYR_BT_CONTROLLER_LL_SW_ULL_PERIPHERAL_ISO_INTERNAL_H_
#define ZEPHYR_BT_CONTROLLER_LL_SW_ULL_PERIPHERAL_ISO_INTERNAL_H_

int ull_peripheral_iso_init(void);
int ull_peripheral_iso_reset(void);

void ull_peripheral_iso_release(uint16_t cis_handle);
uint8_t ull_peripheral_iso_acquire(struct ll_conn *acl,
				   struct pdu_data_llctrl_cis_req *req,
				   uint16_t *cis_handle);
uint8_t ull_peripheral_iso_setup(struct pdu_data_llctrl_cis_ind *ind,
				 uint8_t cig_id,
				 uint16_t cis_handle,
				 uint16_t *conn_event_count);
void ull_peripheral_iso_update_peer_sca(struct ll_conn *acl);
void ull_peripheral_iso_update_ticker(struct ll_conn_iso_group *cig,
				      uint32_t ticks_at_expire,
				      uint32_t iso_interval_us_frac);

#endif /* ZEPHYR_BT_CONTROLLER_LL_SW_ULL_PERIPHERAL_ISO_INTERNAL_H_ */
