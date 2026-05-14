/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_MESHTASTIC_CHANNELS_H_
#define ZEPHYR_SUBSYS_MESHTASTIC_CHANNELS_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "meshtastic/channel.pb.h"
#include "meshtastic/config.pb.h"

#include <zephyr/meshtastic/meshtastic.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Channel table size (fixed by the Meshtastic protobuf schema). */
#define MESHTASTIC_MAX_CHANNELS 8U

struct meshtastic_channel_key {
	uint8_t bytes[32];
	size_t len;
};

int meshtastic_channels_init_defaults(void);
int meshtastic_channels_init_from_config(const struct meshtastic_config *cfg);

uint8_t meshtastic_channels_count(void);
uint8_t meshtastic_channels_primary_index(void);
const meshtastic_Channel *meshtastic_channels_get(uint8_t index);
int meshtastic_channels_set_slot(uint8_t index, const meshtastic_Channel *channel);

int meshtastic_channels_get_key(uint8_t index, struct meshtastic_channel_key *key);
uint8_t meshtastic_channels_get_hash(uint8_t index);
bool meshtastic_channels_decrypt_for_hash(uint8_t index, uint8_t wire_hash);
const char *meshtastic_channels_get_name(uint8_t index);

uint8_t meshtastic_channels_resolve_send_index(uint32_t dest, uint8_t channel_index,
					       uint8_t wire_hash);

uint8_t meshtastic_channels_primary_hash(void);
const char *meshtastic_channels_primary_name(void);
int meshtastic_channels_primary_key(struct meshtastic_channel_key *key);

bool meshtastic_channels_uplink_enabled(uint8_t index);
bool meshtastic_channels_downlink_enabled(uint8_t index);
bool meshtastic_channels_matches_mqtt_name(const char *channel_id);

meshtastic_Config_DeviceConfig_Role meshtastic_device_role(void);
meshtastic_Config_DeviceConfig_RebroadcastMode meshtastic_rebroadcast_mode(void);
void meshtastic_set_device_role(meshtastic_Config_DeviceConfig_Role role);
void meshtastic_set_rebroadcast_mode(meshtastic_Config_DeviceConfig_RebroadcastMode mode);

bool meshtastic_is_rebroadcaster(void);
bool meshtastic_decode_known_only(uint32_t from);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SUBSYS_MESHTASTIC_CHANNELS_H_ */
