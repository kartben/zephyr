/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_BT_MESH_MESH_H_
#define ZEPHYR_BT_MESH_MESH_H_

struct bt_mesh_net;

int bt_mesh_start(void);
void bt_mesh_reprovision(uint16_t addr);
void bt_mesh_dev_key_cand(const uint8_t *key);
void bt_mesh_dev_key_cand_remove(void);
void bt_mesh_dev_key_cand_activate(void);

#endif /* ZEPHYR_BT_MESH_MESH_H_ */
