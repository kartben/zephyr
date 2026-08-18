/** @file
 *  @brief HCI controller user extensions
 */

/*
 * Copyright (c) 2019 Oticon A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_BT_CONTROLLER_HCI_HCI_USER_EXT_H_
#define ZEPHYR_BT_CONTROLLER_HCI_HCI_USER_EXT_H_

int8_t hci_user_ext_get_class(struct node_rx_pdu *node_rx);
void hci_user_ext_encode_control(struct node_rx_pdu *node_rx, struct pdu_data *pdu_data, struct net_buf *buf);

#endif /* ZEPHYR_BT_CONTROLLER_HCI_HCI_USER_EXT_H_ */
