/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_MESHTASTIC_ROUTER_H_
#define ZEPHYR_SUBSYS_MESHTASTIC_ROUTER_H_

#include <stddef.h>
#include <stdint.h>

#include "meshtastic_core.h"

#ifdef __cplusplus
extern "C" {
#endif

void meshtastic_router_process_lora_rx(const uint8_t *buf, int len, int16_t rssi, int8_t snr);
void meshtastic_handle_inbound_packet(const struct meshtastic_packet *packet, const uint8_t *wire,
				      size_t wire_len, bool decoded);
int meshtastic_inject_downlink_mesh_packet(const meshtastic_MeshPacket *mesh);
void meshtastic_routing_sniff_rebroadcast(const struct meshtastic_wire_header *hdr,
					  const uint8_t *wire, size_t wire_len,
					  const struct meshtastic_packet *packet);
void meshtastic_routing_on_decoded(const struct meshtastic_packet *packet);
void meshtastic_routing_sniff(const struct meshtastic_wire_header *hdr, const uint8_t *wire,
			      size_t wire_len, const struct meshtastic_packet *packet,
			      bool decoded);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SUBSYS_MESHTASTIC_ROUTER_H_ */
