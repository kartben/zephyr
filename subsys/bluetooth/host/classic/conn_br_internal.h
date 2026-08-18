/** @file
 *  @brief Internal APIs for Bluetooth classic connection handling.
 */

/*
 * Copyright (c) 2015 Intel Corporation
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_BT_HOST_CLASSIC_CONN_BR_INTERNAL_H_
#define ZEPHYR_BT_HOST_CLASSIC_CONN_BR_INTERNAL_H_

int bt_hci_connect_br_cancel(struct bt_conn *conn);

void bt_br_acl_recv(struct bt_conn *conn, struct net_buf *buf, bool complete);

#endif /* ZEPHYR_BT_HOST_CLASSIC_CONN_BR_INTERNAL_H_ */
