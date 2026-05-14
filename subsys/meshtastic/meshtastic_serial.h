/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_MESHTASTIC_SERIAL_H_
#define ZEPHYR_SUBSYS_MESHTASTIC_SERIAL_H_

#include <stddef.h>
#include <stdint.h>

#include "meshtastic_phoneapi.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_MESHTASTIC_SERIAL)
int meshtastic_serial_init(void);
size_t meshtastic_serial_encode_frame(const uint8_t *payload, size_t payload_len, uint8_t *out,
				      size_t out_len);
int meshtastic_serial_decode_byte(uint8_t byte, uint8_t *payload, size_t payload_len,
				  size_t *frame_len);
#else
static inline int meshtastic_serial_init(void)
{
	return 0;
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SUBSYS_MESHTASTIC_SERIAL_H_ */
