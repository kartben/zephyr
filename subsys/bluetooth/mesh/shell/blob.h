/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_BT_MESH_SHELL_BLOB_H_
#define ZEPHYR_BT_MESH_SHELL_BLOB_H_

#include <zephyr/shell/shell.h>

extern const struct bt_mesh_blob_io *bt_mesh_shell_blob_io;
extern bool bt_mesh_shell_blob_valid;

void bt_mesh_shell_blob_cmds_init(void);

#endif /* ZEPHYR_BT_MESH_SHELL_BLOB_H_ */
