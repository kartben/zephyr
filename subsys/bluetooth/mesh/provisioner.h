/*
 * Copyright (c) 2017 Intel Corporation
 * Copyright (c) 2020 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_BT_MESH_PROVISIONER_H_
#define ZEPHYR_BT_MESH_PROVISIONER_H_

int bt_mesh_pb_adv_open(const uint8_t uuid[16], uint16_t net_idx, uint16_t addr,
			uint8_t attention_duration);

int bt_mesh_pb_gatt_open(const uint8_t uuid[16], uint16_t net_idx, uint16_t addr,
			 uint8_t attention_duration);

#endif /* ZEPHYR_BT_MESH_PROVISIONER_H_ */
