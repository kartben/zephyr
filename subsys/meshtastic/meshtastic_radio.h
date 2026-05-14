/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_MESHTASTIC_RADIO_H_
#define ZEPHYR_SUBSYS_MESHTASTIC_RADIO_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int meshtastic_radio_init(void);
int meshtastic_radio_send_wire(uint8_t *pkt, uint32_t pkt_len);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SUBSYS_MESHTASTIC_RADIO_H_ */
