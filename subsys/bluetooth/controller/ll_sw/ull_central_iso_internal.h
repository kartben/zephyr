/*
 * Copyright (c) 2021 Demant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Helper functions to initialize and reset ull_central_iso module */

#ifndef ZEPHYR_BT_CONTROLLER_LL_SW_ULL_CENTRAL_ISO_INTERNAL_H_
#define ZEPHYR_BT_CONTROLLER_LL_SW_ULL_CENTRAL_ISO_INTERNAL_H_

int ull_central_iso_init(void);
int ull_central_iso_reset(void);

int ull_central_iso_cis_offset_get(uint16_t cis_handle,
				   uint32_t *cis_offset_min,
				   uint32_t *cis_offset_max,
				   uint16_t *conn_event_count);
uint8_t ull_central_iso_setup(uint16_t cis_handle,
			      uint32_t *cig_sync_delay,
			      uint32_t *cis_sync_delay,
			      uint32_t *cis_offset_min,
			      uint32_t *cis_offset_max,
			      uint16_t *conn_event_count,
			      uint8_t  *access_addr);
bool ull_central_iso_all_cises_terminated(struct ll_conn_iso_group *cig);

#endif /* ZEPHYR_BT_CONTROLLER_LL_SW_ULL_CENTRAL_ISO_INTERNAL_H_ */
