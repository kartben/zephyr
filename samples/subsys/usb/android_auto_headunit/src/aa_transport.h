/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_TRANSPORT_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_TRANSPORT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

/*
 * Byte stream transport between the head unit and the protocol layer: USB
 * accessory bulk endpoints or a TCP connection. Frames are parsed from the
 * stream by the messenger, the transport never looks at them.
 */
struct aa_transport {
	/** One-time bring-up, returns 0 on success. */
	int (*open)(void);
	/** Block until a peer is attached: 0, or -ETIMEDOUT. */
	int (*wait_link)(k_timeout_t timeout);
	/** Read up to len bytes: bytes read, -ETIMEDOUT or -ENOTCONN. */
	int (*read)(uint8_t *buf, size_t len, k_timeout_t timeout);
	/** Write all len bytes: 0, -ENOTCONN or -ETIMEDOUT. */
	int (*write)(const uint8_t *buf, size_t len);
	/** Drop the peer so that wait_link() can be used again. */
	void (*close)(void);
	bool (*is_up)(void);
	const char *name;
};

/** @brief The transport selected at build time. */
const struct aa_transport *aa_transport_get(void);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_TRANSPORT_H_ */
