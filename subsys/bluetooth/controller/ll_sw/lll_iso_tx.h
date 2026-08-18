/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 * Copyright (c) 2021 Demant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_BT_CONTROLLER_LL_SW_LLL_ISO_TX_H_
#define ZEPHYR_BT_CONTROLLER_LL_SW_LLL_ISO_TX_H_

struct node_tx_iso {
	union {
		void        *next;
		memq_link_t *link;
	};

	uint64_t payload_count:39; /* bisPayloadCounter/cisPayloadCounter */
	uint64_t sdu_fragments : 8;
	uint8_t  pdu[];
};

#endif /* ZEPHYR_BT_CONTROLLER_LL_SW_LLL_ISO_TX_H_ */
