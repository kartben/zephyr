/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_BT_CONTROLLER_LL_SW_PDU_DF_H_
#define ZEPHYR_BT_CONTROLLER_LL_SW_PDU_DF_H_

struct pdu_cte_info {
#ifdef CONFIG_LITTLE_ENDIAN
	uint8_t  time:5;
	uint8_t  rfu:1;
	uint8_t  type:2;
#else
	uint8_t  type:2;
	uint8_t  rfu:1;
	uint8_t  time:5;
#endif /* CONFIG_LITTLE_ENDIAN */
} __packed;

#endif /* ZEPHYR_BT_CONTROLLER_LL_SW_PDU_DF_H_ */
