/*
 * Copyright (c) 2017 Intel Corporation
 * Copyright (c) 2021 Nordic Semiconductor ASA
 * Copyright (c) 2022 Xiaomi Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_BT_MESH_PB_GATT_H_
#define ZEPHYR_BT_MESH_PB_GATT_H_

int bt_mesh_pb_gatt_start(struct bt_conn *conn);
int bt_mesh_pb_gatt_close(struct bt_conn *conn);
int bt_mesh_pb_gatt_recv(struct bt_conn *conn, struct net_buf_simple *buf);

int bt_mesh_pb_gatt_cli_open(struct bt_conn *conn);
int bt_mesh_pb_gatt_cli_start(struct bt_conn *conn);

#endif /* ZEPHYR_BT_MESH_PB_GATT_H_ */
