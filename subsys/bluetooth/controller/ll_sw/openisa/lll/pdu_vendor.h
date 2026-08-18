/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_BT_CONTROLLER_LL_SW_OPENISA_LLL_PDU_VENDOR_H_
#define ZEPHYR_BT_CONTROLLER_LL_SW_OPENISA_LLL_PDU_VENDOR_H_

#include <zephyr/sys/util.h>

/* Minimum vendor specific Rx payload buffer allocation */
#define LL_VND_OCTETS_RX_MIN 0

/* No vendor Data PDU struct octet3 */
struct pdu_data_vnd_octet3 {
	FLEXIBLE_ARRAY_DECLARE(uint8_t, resv);
} __packed;

/* No vendor BIS PDU struct octet3 */
struct pdu_bis_vnd_octet3 {
	FLEXIBLE_ARRAY_DECLARE(uint8_t, resv);
} __packed;

/* No vendor CIS PDU struct octet3 */
struct pdu_cis_vnd_octet3 {
	FLEXIBLE_ARRAY_DECLARE(uint8_t, resv);
} __packed;

/* No ISOAL helper vendor ISO PDU struct octet3 */
struct pdu_iso_vnd_octet3 {
	FLEXIBLE_ARRAY_DECLARE(uint8_t, resv);
} __packed;

#endif /* ZEPHYR_BT_CONTROLLER_LL_SW_OPENISA_LLL_PDU_VENDOR_H_ */
