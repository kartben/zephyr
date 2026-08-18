/*
 * Copyright (c) 2021 Xiaomi Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_BT_MESH_PB_GATT_CLI_H_
#define ZEPHYR_BT_MESH_PB_GATT_CLI_H_

int bt_mesh_pb_gatt_cli_setup(const uint8_t uuid[16]);

void bt_mesh_pb_gatt_cli_adv_recv(const struct bt_le_scan_recv_info *info,
				  struct net_buf_simple *buf);

#endif /* ZEPHYR_BT_MESH_PB_GATT_CLI_H_ */
