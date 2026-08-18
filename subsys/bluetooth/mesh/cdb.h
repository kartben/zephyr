/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_BT_MESH_CDB_H_
#define ZEPHYR_BT_MESH_CDB_H_

void bt_mesh_cdb_node_store(const struct bt_mesh_cdb_node *node);
void bt_mesh_cdb_pending_store(void);

#endif /* ZEPHYR_BT_MESH_CDB_H_ */
