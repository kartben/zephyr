/*
 * Copyright (c) 2016 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_BT_CONTROLLER_LL_SW_OPENISA_HCI_HCI_VENDOR_H_
#define ZEPHYR_BT_CONTROLLER_LL_SW_OPENISA_HCI_HCI_VENDOR_H_

#define BT_HCI_VS_HW_PLAT 0
#define BT_HCI_VS_HW_VAR  0

/* Map vendor command handler directly to common implementation */
inline int hci_vendor_cmd_handle(uint16_t ocf, struct net_buf *cmd,
				 struct net_buf **evt)
{
	return hci_vendor_cmd_handle_common(ocf, cmd, evt);
}

#endif /* ZEPHYR_BT_CONTROLLER_LL_SW_OPENISA_HCI_HCI_VENDOR_H_ */
