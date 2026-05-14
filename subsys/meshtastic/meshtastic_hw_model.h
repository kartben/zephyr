/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_MESHTASTIC_HW_MODEL_H_
#define ZEPHYR_SUBSYS_MESHTASTIC_HW_MODEL_H_

#include "meshtastic/mesh.pb.h"

#if defined(CONFIG_BOARD_TWATCH_S3)
#define MESHTASTIC_BOARD_HW_MODEL meshtastic_HardwareModel_T_WATCH_S3
#elif defined(CONFIG_BOARD_TTGO_TBEAM)
#define MESHTASTIC_BOARD_HW_MODEL meshtastic_HardwareModel_TBEAM
#elif defined(CONFIG_BOARD_TTGO_LORA32)
#define MESHTASTIC_BOARD_HW_MODEL meshtastic_HardwareModel_TLORA_V2_1_1P8
#elif defined(CONFIG_BOARD_HELTEC_WIFI_LORA32_V3)
#define MESHTASTIC_BOARD_HW_MODEL meshtastic_HardwareModel_HELTEC_V3
#elif defined(CONFIG_BOARD_HELTEC_WIFI_LORA32_V2)
#define MESHTASTIC_BOARD_HW_MODEL meshtastic_HardwareModel_HELTEC_V2_1
#elif defined(CONFIG_BOARD_HELTEC_WIRELESS_TRACKER)
#define MESHTASTIC_BOARD_HW_MODEL meshtastic_HardwareModel_HELTEC_WIRELESS_TRACKER
#elif defined(CONFIG_BOARD_HELTEC_WIRELESS_STICK_LITE_V3)
#define MESHTASTIC_BOARD_HW_MODEL meshtastic_HardwareModel_HELTEC_WSL_V3
#elif defined(CONFIG_BOARD_HELTEC_T114_V2)
#define MESHTASTIC_BOARD_HW_MODEL meshtastic_HardwareModel_HELTEC_MESH_NODE_T114
#elif defined(CONFIG_BOARD_RAK4631)
#define MESHTASTIC_BOARD_HW_MODEL meshtastic_HardwareModel_RAK4631
#else
#define MESHTASTIC_BOARD_HW_MODEL meshtastic_HardwareModel_PRIVATE_HW
#endif

static inline meshtastic_HardwareModel meshtastic_board_hw_model(void)
{
	return MESHTASTIC_BOARD_HW_MODEL;
}

#endif /* ZEPHYR_SUBSYS_MESHTASTIC_HW_MODEL_H_ */
