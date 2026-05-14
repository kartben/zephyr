/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_MESHTASTIC_MQTT_H_
#define ZEPHYR_SUBSYS_MESHTASTIC_MQTT_H_

#include "meshtastic_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_MESHTASTIC_MQTT)
int meshtastic_mqtt_init(void);
void meshtastic_mqtt_on_tx(const struct meshtastic_packet *packet, const uint8_t *wire,
			   size_t wire_len);
void meshtastic_mqtt_on_rx(const struct meshtastic_packet *packet, const uint8_t *wire,
			   size_t wire_len);
#else
static inline int meshtastic_mqtt_init(void)
{
	return 0;
}

static inline void meshtastic_mqtt_on_tx(const struct meshtastic_packet *packet,
					 const uint8_t *wire, size_t wire_len)
{
	ARG_UNUSED(packet);
	ARG_UNUSED(wire);
	ARG_UNUSED(wire_len);
}

static inline void meshtastic_mqtt_on_rx(const struct meshtastic_packet *packet,
					 const uint8_t *wire, size_t wire_len)
{
	ARG_UNUSED(packet);
	ARG_UNUSED(wire);
	ARG_UNUSED(wire_len);
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SUBSYS_MESHTASTIC_MQTT_H_ */
