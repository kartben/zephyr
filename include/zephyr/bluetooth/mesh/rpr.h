/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for Bluetooth Mesh Remote Provisioning common definitions.
 * @ingroup bt_mesh_rpr
 */

#ifndef ZEPHYR_INCLUDE_BLUETOOTH_MESH_RPR_H_
#define ZEPHYR_INCLUDE_BLUETOOTH_MESH_RPR_H_

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/mesh/main.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup bt_mesh_rpr Remote Provisioning models
 * @ingroup bt_mesh
 * @{
 */

/** Unprovisioned device has URI hash value */
#define BT_MESH_RPR_UNPROV_HASH           BIT(0)
#define BT_MESH_RPR_UNPROV_ACTIVE         BIT(1) /**< Internal */
#define BT_MESH_RPR_UNPROV_FOUND          BIT(2) /**< Internal */
#define BT_MESH_RPR_UNPROV_REPORTED       BIT(3) /**< Internal */
#define BT_MESH_RPR_UNPROV_EXT            BIT(4) /**< Internal */
#define BT_MESH_RPR_UNPROV_HAS_LINK       BIT(5) /**< Internal */
#define BT_MESH_RPR_UNPROV_EXT_ADV_RXD    BIT(6) /**< Internal */

/** Minimum extended scan duration in seconds */
#define BT_MESH_RPR_EXT_SCAN_TIME_MIN 1
/** Maximum extended scan duration in seconds */
#define BT_MESH_RPR_EXT_SCAN_TIME_MAX 21

/** Remote Provisioning status codes. */
enum bt_mesh_rpr_status {
	/** Success. */
	BT_MESH_RPR_SUCCESS,
	/** Scanning cannot start. */
	BT_MESH_RPR_ERR_SCANNING_CANNOT_START,
	/** Invalid state. */
	BT_MESH_RPR_ERR_INVALID_STATE,
	/** Limited resources. */
	BT_MESH_RPR_ERR_LIMITED_RESOURCES,
	/** Link cannot open. */
	BT_MESH_RPR_ERR_LINK_CANNOT_OPEN,
	/** Link open failed. */
	BT_MESH_RPR_ERR_LINK_OPEN_FAILED,
	/** Link closed by device. */
	BT_MESH_RPR_ERR_LINK_CLOSED_BY_DEVICE,
	/** Link closed by server. */
	BT_MESH_RPR_ERR_LINK_CLOSED_BY_SERVER,
	/** Link closed by client. */
	BT_MESH_RPR_ERR_LINK_CLOSED_BY_CLIENT,
	/** Link closed as cannot receive PDU. */
	BT_MESH_RPR_ERR_LINK_CLOSED_AS_CANNOT_RECEIVE_PDU,
	/** Link closed as cannot send PDU. */
	BT_MESH_RPR_ERR_LINK_CLOSED_AS_CANNOT_SEND_PDU,
	/** Link closed as cannot deliver PDU report. */
	BT_MESH_RPR_ERR_LINK_CLOSED_AS_CANNOT_DELIVER_PDU_REPORT,
};

/** Remote Provisioning Server scan state. */
enum bt_mesh_rpr_scan {
	/** No scanning is in progress. */
	BT_MESH_RPR_SCAN_IDLE,
	/** Scanning for multiple devices. */
	BT_MESH_RPR_SCAN_MULTI,
	/** Scanning for a single device. */
	BT_MESH_RPR_SCAN_SINGLE,
};

/** Remote Provisioning Node Refresh procedures. */
enum bt_mesh_rpr_node_refresh {
	/** Change the Device key. */
	BT_MESH_RPR_NODE_REFRESH_DEVKEY,
	/** Change the Device key and address. Device composition may change. */
	BT_MESH_RPR_NODE_REFRESH_ADDR,
	/** Change the Device key and composition. */
	BT_MESH_RPR_NODE_REFRESH_COMPOSITION,
};

/** Remote Provisioning link state. */
enum bt_mesh_rpr_link_state {
	/** Link is idle. */
	BT_MESH_RPR_LINK_IDLE,
	/** Link is opening. */
	BT_MESH_RPR_LINK_OPENING,
	/** Link is active. */
	BT_MESH_RPR_LINK_ACTIVE,
	/** Outbound PDU transfer is in progress. */
	BT_MESH_RPR_LINK_SENDING,
	/** Link is closing. */
	BT_MESH_RPR_LINK_CLOSING,
};

/** Remote provisioning actor, as seen across the mesh. */
struct bt_mesh_rpr_node {
	/** Unicast address of the node. */
	uint16_t addr;
	/** Network index used when communicating with the node. */
	uint16_t net_idx;
	/** Time To Live value used when communicating with the node. */
	uint8_t ttl;
};

/** Unprovisioned device. */
struct bt_mesh_rpr_unprov {
	/** Unprovisioned device UUID. */
	uint8_t uuid[16];
	/** Flags, see @p BT_MESH_RPR_UNPROV_* flags. */
	uint8_t flags;
	/** RSSI of unprovisioned device, as received by server. */
	int8_t rssi;
	/** Out of band information. */
	bt_mesh_prov_oob_info_t oob;
	/** URI hash in unprovisioned beacon.
	 *
	 *  Only valid if @c flags has @ref BT_MESH_RPR_UNPROV_HASH set.
	 */
	uint32_t hash;
};

/** Remote Provisioning Link status */
struct bt_mesh_rpr_link {
	/** Link status */
	enum bt_mesh_rpr_status status;
	/** Link state */
	enum bt_mesh_rpr_link_state state;
};

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_BLUETOOTH_MESH_RPR_H_ */
